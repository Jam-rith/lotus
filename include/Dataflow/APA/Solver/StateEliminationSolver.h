#ifndef DATAFLOW_APA_ENGINES_STATEELIMINATIONSOLVER_H_
#define DATAFLOW_APA_ENGINES_STATEELIMINATIONSOLVER_H_

#include "Dataflow/APA/Importance/Dynamic/IncrementalOrderingHeap.h"
#include "Dataflow/APA/Importance/Dynamic/LinearOrderingModel.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/OrderTrace.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/StateEliminationImportance.h"
#include "Dataflow/APA/Solver/SolverContext.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace elimination {
namespace detail {

inline bool shouldReportStateProgress(const EliminationOptions &Opts,
                                      std::size_t Step, std::size_t Total) {
  if (Opts.OrderProgressInterval == 0 &&
      Opts.OrderProgressTimeIntervalSec <= 0.0) {
    return false;
  }
  const auto Done = Step + 1;
  if (Done == 1 || Done == Total) {
    return true;
  }
  if (Opts.OrderProgressInterval == 0) {
    return false;
  }
  return Done % Opts.OrderProgressInterval == 0;
}

inline bool shouldReportStateProgressByStep(const EliminationOptions &Opts,
                                            std::size_t Step,
                                            std::size_t Total) {
  if (Opts.OrderProgressInterval == 0) {
    return false;
  }
  const auto Done = Step + 1;
  return Done == 1 || Done == Total ||
         Done % Opts.OrderProgressInterval == 0;
}

inline bool shouldReportStateProgressByTime(
    const EliminationOptions &Opts, std::chrono::steady_clock::time_point Now,
    std::chrono::steady_clock::time_point &LastReport) {
  if (Opts.OrderProgressTimeIntervalSec <= 0.0) {
    return false;
  }
  const auto Elapsed =
      std::chrono::duration<double>(Now - LastReport).count();
  if (Elapsed < Opts.OrderProgressTimeIntervalSec) {
    return false;
  }
  LastReport = Now;
  return true;
}

inline std::pair<std::size_t, std::size_t> readSelfMemoryKb() {
  std::ifstream In("/proc/self/status");
  std::size_t RSS = 0;
  std::size_t Peak = 0;
  std::string Line;
  while (std::getline(In, Line)) {
    if (Line.rfind("VmRSS:", 0) == 0) {
      std::istringstream SS(Line.substr(6));
      std::size_t Value = 0;
      SS >> Value;
      RSS = Value;
    } else if (Line.rfind("VmHWM:", 0) == 0) {
      std::istringstream SS(Line.substr(6));
      std::size_t Value = 0;
      SS >> Value;
      Peak = Value;
    }
  }
  return {RSS, Peak};
}

template <typename AnalysisDomainTy>
void reportStateProgress(IntraEliminationSolverContext<AnalysisDomainTy> &Ctx,
                         std::size_t Step, std::size_t Total,
                         const char *Kind = "progress") {
  if (Ctx.Opts.OrderProgressInterval == 0 &&
      Ctx.Opts.OrderProgressTimeIntervalSec <= 0.0) {
    return;
  }

  const auto Memory = readSelfMemoryKb();
  llvm::errs() << "[apa-" << Kind << "]";
  if (!Ctx.Opts.OrderProgressTag.empty()) {
    llvm::errs() << " tag=" << Ctx.Opts.OrderProgressTag;
  }
  llvm::errs() << " eliminated=" << (Step + 1) << "/" << Total
               << " rss_kb=" << Memory.first
               << " peak_rss_kb=" << Memory.second << "\n";
}

template <typename AnalysisDomainTy>
void eliminateStateAt(IntraEliminationSolverContext<AnalysisDomainTy> &Ctx,
                      std::size_t K,
                      std::unordered_set<std::size_t> *Affected = nullptr) {
  using Context = IntraEliminationSolverContext<AnalysisDomainTy>;
  const auto N = Ctx.Nodes.size();
  std::vector<typename Context::expr_ref_t> ColK(N);
  std::vector<typename Context::expr_ref_t> RowK(N);

  for (std::size_t i = 0; i < N; ++i) {
    ColK[i] = Ctx.Matrix[i][K];
  }
  for (std::size_t j = 0; j < N; ++j) {
    RowK[j] = Ctx.Matrix[K][j];
  }

  const auto KStar = Ctx.Exprs.star(Ctx.Matrix[K][K]);
  for (std::size_t i = 0; i < N; ++i) {
    if (Context::expr_factory_t::isZero(ColK[i])) {
      continue;
    }
    if (Affected != nullptr && i != K) {
      Affected->insert(i);
    }
    for (std::size_t j = 0; j < N; ++j) {
      if (Context::expr_factory_t::isZero(RowK[j])) {
        continue;
      }
      if (Affected != nullptr && j != K) {
        Affected->insert(j);
      }
      auto Via = Ctx.Exprs.concat(ColK[i], KStar);
      Via = Ctx.Exprs.concat(Via, RowK[j]);
      Ctx.Matrix[i][j] = Ctx.Exprs.unite(Ctx.Matrix[i][j], Via);
    }
  }
}

// Generic Floyd-Warshall-style elimination over the full CFG. This engine
// makes no reducibility assumptions and therefore serves as the baseline
// implementation as well as the fallback when ADT-specific preconditions fail.
template <typename AnalysisDomainTy>
std::vector<std::size_t> getOriginalStateEliminationOrder(
    const IntraEliminationSolverContext<AnalysisDomainTy> &Ctx) {
  using Context = IntraEliminationSolverContext<AnalysisDomainTy>;
  const auto N = Ctx.Nodes.size();
  std::vector<std::size_t> Order(N);
  const auto *R =
      dynamic_cast<const typename Context::ReducibleProblemTy *>(&Ctx.Problem);
  if (R != nullptr) {
    const auto Topo = R->topologicalOrder();
    if (Topo.size() == N) {
      for (std::size_t i = 0; i < N; ++i) {
        const auto It = Ctx.Index.find(Topo[N - 1 - i]);
        if (It == Ctx.Index.end()) {
          goto DefaultOrder;
        }
        Order[i] = It->second;
      }
      return Order;
    }
  }
DefaultOrder:
  for (std::size_t i = 0; i < N; ++i) {
    Order[i] = i;
  }
  return Order;
}

template <typename AnalysisDomainTy>
void buildStateEliminationMatrix(
    IntraEliminationSolverContext<AnalysisDomainTy> &Ctx) {
  // Build the usual elimination matrix where M[i][j] summarizes all direct
  // edges from node i to node j. Diagonals start at one() so paths are allowed
  // to stay at a node before additional eliminations introduce loops.
  Ctx.Nodes = Ctx.Problem.nodes();
  Ctx.Index.clear();
  Ctx.Index.reserve(Ctx.Nodes.size());
  for (std::size_t i = 0; i < Ctx.Nodes.size(); ++i) {
    Ctx.Index.emplace(Ctx.Nodes[i], i);
  }

  const auto N = Ctx.Nodes.size();
  Ctx.Matrix.assign(
      N,
      std::vector<
          typename IntraEliminationSolverContext<AnalysisDomainTy>::expr_ref_t>(
          N, Ctx.Exprs.zero()));
  for (std::size_t i = 0; i < N; ++i) {
    Ctx.Matrix[i][i] = Ctx.Exprs.one();
  }

  for (const auto &Src : Ctx.Nodes) {
    const auto SrcIdx = Ctx.idx(Src);
    for (const auto &Dst : Ctx.Problem.succs(Src)) {
      const auto It = Ctx.Index.find(Dst);
      if (It == Ctx.Index.end()) {
        continue;
      }
      const auto DstIdx = It->second;
      Ctx.Matrix[SrcIdx][DstIdx] =
          Ctx.Exprs.unite(Ctx.Matrix[SrcIdx][DstIdx],
                          Ctx.Exprs.atom(Ctx.Problem.edgeTransfer(Src, Dst)));
    }
  }
}

template <typename AnalysisDomainTy>
std::vector<std::size_t> getDynamicStateEliminationOrder(
    IntraEliminationSolverContext<AnalysisDomainTy> &Ctx) {
  using Context = IntraEliminationSolverContext<AnalysisDomainTy>;
  using HeapEntry = std::pair<double, std::size_t>;
  using importance_collector_t =
      StateEliminationImportanceCollector<typename Context::expr_factory_t>;

  const auto N = Ctx.Nodes.size();
  std::vector<bool> Alive(N, true);
  std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>>
      MinHeap;
  importance_collector_t ImportanceCollector(Ctx.Exprs);
  StateEliminationImportancePolicy<std::size_t> ImportancePolicy(
      Ctx.Opts.OrderHeuristic);
  LinearOrderingModel LinearModel;
  const bool UseLinearModel =
      Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::LearnedCost &&
      LinearModel.loadFromFile(Ctx.Opts.OrderModelPath);

  auto baseFeatureScore = [&](std::size_t Node) {
    return ImportanceCollector.score(Ctx.Matrix, Alive, Node, ImportancePolicy);
  };

  auto buildOrderTraceRow = [&](std::size_t Step, std::size_t Node,
                                const OrderTraceMatrixStats &MatrixStats) {
    OrderTraceRow Row;
    Row.TraceTag = Ctx.Opts.OrderTraceTag;
    Row.Step = Step;
    Row.NodeIndex = Node;
    Row.Score = baseFeatureScore(Node);
    Row.Candidate = ImportanceCollector.collect(Ctx.Matrix, Alive, Node).Candidate;
    Row.MatrixBefore = MatrixStats;
    return Row;
  };

  auto buildLightweightOrderRow = [&](std::size_t Node,
                                      const OrderingFeatureNeeds &Needs) {
    auto Row =
        ImportanceCollector.collectOrderTraceRow(Ctx.Matrix, Alive, Node, Needs);
    Row.Score = baseFeatureScore(Node);
    return Row;
  };

  auto nodeScore = [&](std::size_t Node) {
    return static_cast<double>(baseFeatureScore(Node));
  };

  OrderTraceMatrixStats LinearMatrixStatsCache;
  bool LinearMatrixStatsDirty = true;
  auto currentLinearMatrixStats = [&]() {
    if (!LinearModel.featureNeeds().needsAnyMatrixFeature()) {
      return OrderTraceMatrixStats();
    }
    if (LinearMatrixStatsDirty) {
      LinearMatrixStatsCache =
          collectOrderTraceMatrixStats<typename Context::expr_factory_t>(
              Ctx.Matrix);
      LinearMatrixStatsDirty = false;
    }
    return LinearMatrixStatsCache;
  };

  auto linearNodeScore = [&](std::size_t Node) {
    auto Row = buildLightweightOrderRow(Node, LinearModel.featureNeeds());
    Row.MatrixBefore = currentLinearMatrixStats();
    return LinearModel.predict(toOrderingFeatureVector(Row));
  };

  auto pushNode = [&](std::size_t Node) {
    MinHeap.emplace(nodeScore(Node), Node);
  };

  IncrementalOrderingHeap LearnedHeap(
      N, [&](std::size_t Node) { return linearNodeScore(Node); },
      [&](std::size_t Node) { return Node < Alive.size() && Alive[Node]; });

  auto collectTraceRowsBeforeChoice = [&](std::size_t Step) {
    std::vector<OrderTraceRow> Rows;
    if (!Ctx.Opts.RecordOrderTrace || Ctx.Opts.OrderTracePath.empty()) {
      return Rows;
    }

    const auto MatrixStats =
        collectOrderTraceMatrixStats<typename Context::expr_factory_t>(
            Ctx.Matrix);
    Rows.reserve(N);
    for (std::size_t Node = 0; Node < N; ++Node) {
      if (!Alive[Node]) {
        continue;
      }
      OrderTraceRow Row;
      Row = buildOrderTraceRow(Step, Node, MatrixStats);
      Rows.push_back(std::move(Row));
    }
    return Rows;
  };

  auto attachChosenTraceLabels =
      [&](std::vector<OrderTraceRow> &Rows, std::size_t ChosenNode,
          const OrderTraceMatrixStats &Before,
          const OrderTraceMatrixStats &After,
          std::size_t AffectedNodeCount, long long ElapsedUs) {
        for (auto &Row : Rows) {
          if (Row.NodeIndex != ChosenNode) {
            continue;
          }
          Row.Chosen = true;
          Row.AffectedNodes = AffectedNodeCount;
          Row.ElapsedUsForChosen = ElapsedUs;
          Row.DeltaTotalUniqueExprNodes =
              static_cast<long long>(After.TotalUniqueExprNodes) -
              static_cast<long long>(Before.TotalUniqueExprNodes);
          Row.DeltaTotalConcatCount =
              static_cast<long long>(After.TotalConcatCount) -
              static_cast<long long>(Before.TotalConcatCount);
          Row.DeltaTotalUnionCount =
              static_cast<long long>(After.TotalUnionCount) -
              static_cast<long long>(Before.TotalUnionCount);
          Row.DeltaTotalStarCount =
              static_cast<long long>(After.TotalStarCount) -
              static_cast<long long>(Before.TotalStarCount);
          return;
        }
      };

  for (std::size_t Node = 0; Node < N; ++Node) {
    if (!UseLinearModel) {
      pushNode(Node);
    }
  }
  if (UseLinearModel) {
    LearnedHeap.initializeAll();
  }

  std::vector<std::size_t> Order;
  Order.reserve(N);
  auto LastTimedProgress = std::chrono::steady_clock::now();

  for (std::size_t Step = 0; Step < N; ++Step) {
    auto TraceRows = collectTraceRowsBeforeChoice(Step);
    const auto MatrixStatsBefore =
        (!TraceRows.empty())
            ? TraceRows.front().MatrixBefore
            : OrderTraceMatrixStats();

    std::size_t K = 0;
    if (UseLinearModel) {
      K = LearnedHeap.popMin();
    } else {
      while (true) {
        const auto Entry = MinHeap.top();
        MinHeap.pop();
        K = Entry.second;
        if (!Alive[K]) {
          continue;
        }

        const auto CurrentScore = nodeScore(K);
        if (std::abs(Entry.first - CurrentScore) < 1e-9) {
          break;
        }
        MinHeap.emplace(CurrentScore, K);
      }
    }

    Order.push_back(K);

    std::unordered_set<std::size_t> Affected;
    const auto EliminateStart = std::chrono::steady_clock::now();
    eliminateStateAt(Ctx, K, &Affected);
    const auto EliminateElapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - EliminateStart)
            .count();

    if (shouldReportStateProgressByStep(Ctx.Opts, Step, N)) {
      reportStateProgress(Ctx, Step, N);
    } else if (shouldReportStateProgressByTime(
                   Ctx.Opts, std::chrono::steady_clock::now(),
                   LastTimedProgress)) {
      reportStateProgress(Ctx, Step, N, "memory");
    }

    if (!TraceRows.empty()) {
      const auto MatrixStatsAfter =
          collectOrderTraceMatrixStats<typename Context::expr_factory_t>(
              Ctx.Matrix);
      attachChosenTraceLabels(TraceRows, K, MatrixStatsBefore, MatrixStatsAfter,
                              Affected.size(), EliminateElapsed);
      appendOrderTraceRows(Ctx.Opts.OrderTracePath, TraceRows);
    }
    LinearMatrixStatsDirty = true;

    Alive[K] = false;
    if (UseLinearModel) {
      LearnedHeap.invalidate(K);
    }
    Affected.erase(K);
    std::vector<std::size_t> RefreshNodes;
    RefreshNodes.reserve(Affected.size());
    for (const auto Node : Affected) {
      if (!Alive[Node]) {
        continue;
      }
      if (!UseLinearModel) {
        pushNode(Node);
      }
      RefreshNodes.push_back(Node);
    }
    if (UseLinearModel) {
      if (LinearModel.featureNeeds().needsAnyMatrixFeature()) {
        for (std::size_t Node = 0; Node < N; ++Node) {
          if (Alive[Node]) {
            LearnedHeap.refresh(Node);
          }
        }
      } else {
        LearnedHeap.refreshAll(RefreshNodes);
      }
    }
    if (Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::MinPredSucc) {
      for (std::size_t Node = 0; Node < N; ++Node) {
        if (Alive[Node]) {
          pushNode(Node);
        }
      }
    }
  }

  return Order;
}

template <typename AnalysisDomainTy>
void eliminateStateIntermediates(
    IntraEliminationSolverContext<AnalysisDomainTy> &Ctx) {
  const auto N = Ctx.Nodes.size();
  if (Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::MinPredSucc ||
      Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::ExpressionAware ||
      Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::StarRisk ||
      Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::LearnedCost) {
    getDynamicStateEliminationOrder(Ctx);
    return;
  }

  const auto Order = getOriginalStateEliminationOrder(Ctx);
  auto LastTimedProgress = std::chrono::steady_clock::now();
  for (std::size_t ki = 0; ki < N; ++ki) {
    const std::size_t K = Order[ki];
    eliminateStateAt(Ctx, K);
    if (shouldReportStateProgressByStep(Ctx.Opts, ki, N)) {
      reportStateProgress(Ctx, ki, N);
    } else if (shouldReportStateProgressByTime(
                   Ctx.Opts, std::chrono::steady_clock::now(),
                   LastTimedProgress)) {
      reportStateProgress(Ctx, ki, N, "memory");
    }
  }
}

template <typename AnalysisDomainTy>
bool materializeStateResults(
    IntraEliminationSolverContext<AnalysisDomainTy> &Ctx) {
  using Context = IntraEliminationSolverContext<AnalysisDomainTy>;
  Ctx.Results = typename Context::result_t{};
  if (Ctx.Nodes.empty()) {
    return true;
  }

  const auto EntryIt = Ctx.Index.find(Ctx.Problem.entry());
  if (EntryIt == Ctx.Index.end()) {
    return false;
  }
  const auto EntryIdx = EntryIt->second;

  const auto Init = Ctx.Problem.initialFact();
  for (std::size_t j = 0; j < Ctx.Nodes.size(); ++j) {
    const auto &N = Ctx.Nodes[j];
    // Each remaining matrix entry summarizes all paths from entry to N.
    auto E = Ctx.Matrix[EntryIdx][j];
    Ctx.Results.ExprTo(N) = E;
    if (!Ctx.Opts.SkipFinalEval) {
      Ctx.Results.IN(N) = Ctx.eval(E, Init);
    }
  }
  return true;
}

template <typename AnalysisDomainTy>
bool solveStateElimination(
    IntraEliminationSolverContext<AnalysisDomainTy> &Ctx) {
  buildStateEliminationMatrix(Ctx);
  eliminateStateIntermediates(Ctx);
  return materializeStateResults(Ctx);
}

} // namespace detail
} // namespace elimination

#endif // DATAFLOW_APA_ENGINES_STATEELIMINATIONSOLVER_H_
