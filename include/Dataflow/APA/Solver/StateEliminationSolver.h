#ifndef DATAFLOW_APA_ENGINES_STATEELIMINATIONSOLVER_H_
#define DATAFLOW_APA_ENGINES_STATEELIMINATIONSOLVER_H_

#include "Dataflow/APA/Solver/SolverContext.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

namespace elimination {
namespace detail {

inline std::size_t saturatingAdd(std::size_t Lhs, std::size_t Rhs) {
  const auto Max = std::numeric_limits<std::size_t>::max();
  if (Max - Lhs < Rhs) {
    return Max;
  }
  return Lhs + Rhs;
}

inline std::size_t saturatingMul(std::size_t Lhs, std::size_t Rhs) {
  const auto Max = std::numeric_limits<std::size_t>::max();
  if (Lhs != 0 && Rhs > Max / Lhs) {
    return Max;
  }
  return Lhs * Rhs;
}

template <typename ExprFactoryT>
std::size_t exprSize(typename ExprFactoryT::Ref Expr,
                     std::unordered_set<const void *> &Seen) {
  if (!Expr) {
    return 0;
  }
  if (!Seen.insert(Expr.get()).second) {
    return 1;
  }

  switch (Expr->K) {
  case ExprFactoryT::Kind::Zero:
  case ExprFactoryT::Kind::One:
  case ExprFactoryT::Kind::Atom:
    return 1;
  case ExprFactoryT::Kind::Star:
    return saturatingAdd(
        static_cast<std::size_t>(1), exprSize<ExprFactoryT>(Expr->L, Seen));
  case ExprFactoryT::Kind::Union:
  case ExprFactoryT::Kind::Concat:
    return saturatingAdd(
        static_cast<std::size_t>(1),
        saturatingAdd(exprSize<ExprFactoryT>(Expr->L, Seen),
                      exprSize<ExprFactoryT>(Expr->R, Seen)));
  }
  return 1;
}

template <typename ExprFactoryT>
std::size_t exprSize(typename ExprFactoryT::Ref Expr) {
  std::unordered_set<const void *> Seen;
  return exprSize<ExprFactoryT>(std::move(Expr), Seen);
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
std::size_t structuralOrderScore(
    const IntraEliminationSolverContext<AnalysisDomainTy> &Ctx,
    std::size_t Node, const std::vector<bool> &Alive,
    const std::vector<std::size_t> &PredCount,
    const std::vector<std::size_t> &SuccCount,
    const std::vector<std::size_t> &ExprSizes) {
  (void)Ctx;
  (void)Node;
  (void)Alive;
  (void)ExprSizes;
  return saturatingMul(PredCount[Node], SuccCount[Node]);
}

template <typename AnalysisDomainTy>
std::size_t expressionAwareOrderScore(
    const IntraEliminationSolverContext<AnalysisDomainTy> &Ctx,
    std::size_t Node, const std::vector<bool> &Alive,
    const std::vector<std::size_t> &PredCount,
    const std::vector<std::size_t> &SuccCount,
    const std::vector<std::size_t> &ExprSizes) {
  using Context = IntraEliminationSolverContext<AnalysisDomainTy>;
  (void)PredCount;
  (void)SuccCount;

  const auto N = Ctx.Nodes.size();
  std::size_t Score = 0;
  const auto LoopCost = std::max<std::size_t>(1, ExprSizes[Node]);
  for (std::size_t i = 0; i < N; ++i) {
    if (!Alive[i] || i == Node ||
        Context::expr_factory_t::isZero(Ctx.Matrix[i][Node])) {
      continue;
    }
    const auto PredCost =
        exprSize<typename Context::expr_factory_t>(Ctx.Matrix[i][Node]);
    for (std::size_t j = 0; j < N; ++j) {
      if (!Alive[j] || j == Node ||
          Context::expr_factory_t::isZero(Ctx.Matrix[Node][j])) {
        continue;
      }
      const auto SuccCost =
          exprSize<typename Context::expr_factory_t>(Ctx.Matrix[Node][j]);
      Score = saturatingAdd(
          Score,
          saturatingAdd(PredCost, saturatingAdd(LoopCost, SuccCost)));
    }
  }
  return Score;
}

template <typename AnalysisDomainTy>
std::size_t starRiskOrderScore(
    const IntraEliminationSolverContext<AnalysisDomainTy> &Ctx,
    std::size_t Node, const std::vector<bool> &Alive,
    const std::vector<std::size_t> &PredCount,
    const std::vector<std::size_t> &SuccCount,
    const std::vector<std::size_t> &ExprSizes) {
  using Context = IntraEliminationSolverContext<AnalysisDomainTy>;
  auto Score = expressionAwareOrderScore(Ctx, Node, Alive, PredCount,
                                         SuccCount, ExprSizes);
  const auto &Self = Ctx.Matrix[Node][Node];
  if (!Context::expr_factory_t::isZero(Self) &&
      !Context::expr_factory_t::isOne(Self)) {
    Score = saturatingAdd(Score, saturatingMul(ExprSizes[Node], Ctx.Nodes.size()));
  }
  return Score;
}

template <typename AnalysisDomainTy>
std::size_t scoreStateEliminationNode(
    const IntraEliminationSolverContext<AnalysisDomainTy> &Ctx,
    std::size_t Node, const std::vector<bool> &Alive,
    const std::vector<std::size_t> &PredCount,
    const std::vector<std::size_t> &SuccCount,
    const std::vector<std::size_t> &ExprSizes) {
  switch (Ctx.Opts.OrderHeuristic) {
  case EliminationOrderHeuristic::MinPredSucc:
    return structuralOrderScore(Ctx, Node, Alive, PredCount, SuccCount,
                                ExprSizes);
  case EliminationOrderHeuristic::ExpressionAware:
    return expressionAwareOrderScore(Ctx, Node, Alive, PredCount, SuccCount,
                                     ExprSizes);
  case EliminationOrderHeuristic::StarRisk:
    return starRiskOrderScore(Ctx, Node, Alive, PredCount, SuccCount,
                              ExprSizes);
  case EliminationOrderHeuristic::Original:
    return structuralOrderScore(Ctx, Node, Alive, PredCount, SuccCount,
                                ExprSizes);
  }
  return structuralOrderScore(Ctx, Node, Alive, PredCount, SuccCount,
                              ExprSizes);
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
  using HeapEntry = std::pair<std::size_t, std::size_t>;

  const auto N = Ctx.Nodes.size();
  std::vector<bool> Alive(N, true);
  std::vector<std::size_t> PredCount(N, 0);
  std::vector<std::size_t> SuccCount(N, 0);
  std::vector<std::size_t> ExprSizes(N, 0);
  std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>>
      MinHeap;

  auto recomputeNode = [&](std::size_t Node) {
    PredCount[Node] = 0;
    SuccCount[Node] = 0;
    for (std::size_t Other = 0; Other < N; ++Other) {
      if (!Alive[Other] || Other == Node) {
        continue;
      }
      if (!Context::expr_factory_t::isZero(Ctx.Matrix[Other][Node])) {
        ++PredCount[Node];
      }
      if (!Context::expr_factory_t::isZero(Ctx.Matrix[Node][Other])) {
        ++SuccCount[Node];
      }
    }
    ExprSizes[Node] =
        exprSize<typename Context::expr_factory_t>(Ctx.Matrix[Node][Node]);
  };

  auto nodeScore = [&](std::size_t Node) {
    return scoreStateEliminationNode(Ctx, Node, Alive, PredCount, SuccCount,
                                     ExprSizes);
  };

  auto pushNode = [&](std::size_t Node) {
    MinHeap.emplace(nodeScore(Node), Node);
  };

  for (std::size_t Node = 0; Node < N; ++Node) {
    recomputeNode(Node);
    pushNode(Node);
  }

  std::vector<std::size_t> Order;
  Order.reserve(N);

  for (std::size_t Step = 0; Step < N; ++Step) {
    std::size_t K = 0;
    while (true) {
      const auto Entry = MinHeap.top();
      MinHeap.pop();
      K = Entry.second;
      if (!Alive[K]) {
        continue;
      }

      const auto CurrentScore = nodeScore(K);
      if (Entry.first == CurrentScore) {
        break;
      }
      MinHeap.emplace(CurrentScore, K);
    }

    Order.push_back(K);

    std::unordered_set<std::size_t> Affected;
    eliminateStateAt(Ctx, K, &Affected);

    Alive[K] = false;
    Affected.erase(K);
    for (const auto Node : Affected) {
      if (!Alive[Node]) {
        continue;
      }
      recomputeNode(Node);
      pushNode(Node);
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
      Ctx.Opts.OrderHeuristic == EliminationOrderHeuristic::StarRisk) {
    getDynamicStateEliminationOrder(Ctx);
    return;
  }

  const auto Order = getOriginalStateEliminationOrder(Ctx);
  for (std::size_t ki = 0; ki < N; ++ki) {
    const std::size_t K = Order[ki];
    eliminateStateAt(Ctx, K);
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
