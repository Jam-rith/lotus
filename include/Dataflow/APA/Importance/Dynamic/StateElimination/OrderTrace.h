#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_ORDERTRACE_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_ORDERTRACE_H_

#include "Dataflow/APA/Importance/Dynamic/OrderingFeatures.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/EliminationMatrixStats.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/PathExpressionStats.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace elimination {

// Whole-matrix expression profile used as a proxy label and optional learned
// ordering feature. Unlike the final result profile, this describes the
// intermediate elimination matrix at one ordering step.
struct OrderTraceMatrixStats final {
  std::size_t TotalUniqueExprNodes = 0;
  std::size_t TotalSharedRefs = 0;
  std::size_t TotalUnionCount = 0;
  std::size_t TotalConcatCount = 0;
  std::size_t TotalStarCount = 0;
  std::size_t MaxDepth = 0;
};

// One row in order_trace.tsv. Rows are emitted for every alive candidate at a
// step; only the chosen row receives non-zero observed local labels.
struct OrderTraceRow final {
  std::string TraceTag;
  std::size_t Step = 0;
  std::size_t NodeIndex = 0;
  bool Chosen = false;
  std::size_t Score = 0;

  EliminationCandidateStats Candidate;
  OrderTraceMatrixStats MatrixBefore;

  std::size_t AffectedNodes = 0;
  long long ElapsedUsForChosen = 0;
  long long DeltaTotalUniqueExprNodes = 0;
  long long DeltaTotalConcatCount = 0;
  long long DeltaTotalUnionCount = 0;
  long long DeltaTotalStarCount = 0;
};

inline OrderingFeatureVector toOrderingFeatureVector(const OrderTraceRow &Row) {
  OrderingFeatureVector Features;
  const auto &C = Row.Candidate;
  Features.Score = static_cast<double>(Row.Score);
  Features.AlivePredCount = static_cast<double>(C.AlivePredCount);
  Features.AliveSuccCount = static_cast<double>(C.AliveSuccCount);
  Features.PredSuccProduct =
      static_cast<double>(C.AlivePredCount * C.AliveSuccCount);
  Features.CrossCombinationCount =
      static_cast<double>(C.CrossCombinationCount);
  Features.FillInCount = static_cast<double>(C.FillInCount);
  Features.ExistingTargetCount = static_cast<double>(C.ExistingTargetCount);
  Features.ExistingViaConcatCount =
      static_cast<double>(C.ExistingViaConcatCount);
  Features.SharedInputRefCount = static_cast<double>(C.SharedInputRefCount);

  Features.SelfUniqueNodes =
      static_cast<double>(C.SelfExprStats.UniqueNodeCount);
  Features.SelfUnionCount = static_cast<double>(C.SelfExprStats.UnionCount);
  Features.SelfConcatCount = static_cast<double>(C.SelfExprStats.ConcatCount);
  Features.SelfStarCount = static_cast<double>(C.SelfExprStats.StarCount);
  Features.SelfMaxDepth = static_cast<double>(C.SelfExprStats.MaxDepth);

  Features.IncomingUniqueNodes =
      static_cast<double>(C.IncomingExprStats.UniqueNodeCount);
  Features.IncomingUnionCount =
      static_cast<double>(C.IncomingExprStats.UnionCount);
  Features.IncomingConcatCount =
      static_cast<double>(C.IncomingExprStats.ConcatCount);
  Features.IncomingStarCount =
      static_cast<double>(C.IncomingExprStats.StarCount);
  Features.IncomingMaxDepth =
      static_cast<double>(C.IncomingExprStats.MaxDepth);

  Features.OutgoingUniqueNodes =
      static_cast<double>(C.OutgoingExprStats.UniqueNodeCount);
  Features.OutgoingUnionCount =
      static_cast<double>(C.OutgoingExprStats.UnionCount);
  Features.OutgoingConcatCount =
      static_cast<double>(C.OutgoingExprStats.ConcatCount);
  Features.OutgoingStarCount =
      static_cast<double>(C.OutgoingExprStats.StarCount);
  Features.OutgoingMaxDepth =
      static_cast<double>(C.OutgoingExprStats.MaxDepth);

  Features.MatrixTotalUniqueExprNodesBefore =
      static_cast<double>(Row.MatrixBefore.TotalUniqueExprNodes);
  Features.MatrixTotalSharedRefsBefore =
      static_cast<double>(Row.MatrixBefore.TotalSharedRefs);
  Features.MatrixTotalConcatCountBefore =
      static_cast<double>(Row.MatrixBefore.TotalConcatCount);
  Features.MatrixTotalUnionCountBefore =
      static_cast<double>(Row.MatrixBefore.TotalUnionCount);
  Features.MatrixTotalStarCountBefore =
      static_cast<double>(Row.MatrixBefore.TotalStarCount);
  Features.MatrixMaxDepthBefore =
      static_cast<double>(Row.MatrixBefore.MaxDepth);
  return Features;
}

inline bool shouldWriteOrderTraceHeader(const std::string &Path) {
  std::ifstream In(Path);
  return !In.good() || In.peek() == std::ifstream::traits_type::eof();
}

inline void writeOrderTraceHeader(std::ostream &OS) {
  OS << "trace_tag"
     << '\t' << "step"
     << '\t' << "node_index"
     << '\t' << "chosen"
     << '\t' << "score"
     << '\t' << "alive_pred_count"
     << '\t' << "alive_succ_count"
     << '\t' << "pred_succ_product"
     << '\t' << "cross_combination_count"
     << '\t' << "fill_in_count"
     << '\t' << "existing_target_count"
     << '\t' << "existing_via_concat_count"
     << '\t' << "shared_input_ref_count"
     << '\t' << "self_unique_nodes"
     << '\t' << "self_union_count"
     << '\t' << "self_concat_count"
     << '\t' << "self_star_count"
     << '\t' << "self_max_depth"
     << '\t' << "incoming_unique_nodes"
     << '\t' << "incoming_union_count"
     << '\t' << "incoming_concat_count"
     << '\t' << "incoming_star_count"
     << '\t' << "incoming_max_depth"
     << '\t' << "outgoing_unique_nodes"
     << '\t' << "outgoing_union_count"
     << '\t' << "outgoing_concat_count"
     << '\t' << "outgoing_star_count"
     << '\t' << "outgoing_max_depth"
     << '\t' << "matrix_total_unique_expr_nodes_before"
     << '\t' << "matrix_total_shared_refs_before"
     << '\t' << "matrix_total_concat_count_before"
     << '\t' << "matrix_total_union_count_before"
     << '\t' << "matrix_total_star_count_before"
     << '\t' << "matrix_max_depth_before"
     << '\t' << "affected_nodes"
     << '\t' << "elapsed_us_for_chosen"
     << '\t' << "delta_total_unique_expr_nodes"
     << '\t' << "delta_total_concat_count"
     << '\t' << "delta_total_union_count"
     << '\t' << "delta_total_star_count"
     << '\n';
}

inline void writeOrderTraceRow(std::ostream &OS, const OrderTraceRow &Row) {
  const auto PredSuccProduct =
      Row.Candidate.AlivePredCount * Row.Candidate.AliveSuccCount;
  OS << Row.TraceTag
     << '\t' << Row.Step
     << '\t' << Row.NodeIndex
     << '\t' << (Row.Chosen ? 1 : 0)
     << '\t' << Row.Score
     << '\t' << Row.Candidate.AlivePredCount
     << '\t' << Row.Candidate.AliveSuccCount
     << '\t' << PredSuccProduct
     << '\t' << Row.Candidate.CrossCombinationCount
     << '\t' << Row.Candidate.FillInCount
     << '\t' << Row.Candidate.ExistingTargetCount
     << '\t' << Row.Candidate.ExistingViaConcatCount
     << '\t' << Row.Candidate.SharedInputRefCount
     << '\t' << Row.Candidate.SelfExprStats.UniqueNodeCount
     << '\t' << Row.Candidate.SelfExprStats.UnionCount
     << '\t' << Row.Candidate.SelfExprStats.ConcatCount
     << '\t' << Row.Candidate.SelfExprStats.StarCount
     << '\t' << Row.Candidate.SelfExprStats.MaxDepth
     << '\t' << Row.Candidate.IncomingExprStats.UniqueNodeCount
     << '\t' << Row.Candidate.IncomingExprStats.UnionCount
     << '\t' << Row.Candidate.IncomingExprStats.ConcatCount
     << '\t' << Row.Candidate.IncomingExprStats.StarCount
     << '\t' << Row.Candidate.IncomingExprStats.MaxDepth
     << '\t' << Row.Candidate.OutgoingExprStats.UniqueNodeCount
     << '\t' << Row.Candidate.OutgoingExprStats.UnionCount
     << '\t' << Row.Candidate.OutgoingExprStats.ConcatCount
     << '\t' << Row.Candidate.OutgoingExprStats.StarCount
     << '\t' << Row.Candidate.OutgoingExprStats.MaxDepth
     << '\t' << Row.MatrixBefore.TotalUniqueExprNodes
     << '\t' << Row.MatrixBefore.TotalSharedRefs
     << '\t' << Row.MatrixBefore.TotalConcatCount
     << '\t' << Row.MatrixBefore.TotalUnionCount
     << '\t' << Row.MatrixBefore.TotalStarCount
     << '\t' << Row.MatrixBefore.MaxDepth
     << '\t' << Row.AffectedNodes
     << '\t' << Row.ElapsedUsForChosen
     << '\t' << Row.DeltaTotalUniqueExprNodes
     << '\t' << Row.DeltaTotalConcatCount
     << '\t' << Row.DeltaTotalUnionCount
     << '\t' << Row.DeltaTotalStarCount
     << '\n';
}

inline void appendOrderTraceRows(const std::string &Path,
                                 const std::vector<OrderTraceRow> &Rows) {
  if (Path.empty() || Rows.empty()) {
    return;
  }

  const bool NeedHeader = shouldWriteOrderTraceHeader(Path);
  std::ofstream OS(Path, std::ios::app);
  if (!OS) {
    return;
  }
  if (NeedHeader) {
    writeOrderTraceHeader(OS);
  }
  for (const auto &Row : Rows) {
    writeOrderTraceRow(OS, Row);
  }
}

template <typename ExprFactoryT> class OrderTraceMatrixStatsCollector final {
public:
  using expr_ref_t = typename ExprFactoryT::Ref;
  using matrix_t = std::vector<std::vector<expr_ref_t>>;

  OrderTraceMatrixStats collect(const matrix_t &Matrix) {
    Stats = OrderTraceMatrixStats();
    Seen.clear();
    DepthMemo.clear();
    for (const auto &Row : Matrix) {
      for (const auto &Expr : Row) {
        const auto Depth = collectImpl(Expr);
        if (Depth > Stats.MaxDepth) {
          Stats.MaxDepth = Depth;
        }
      }
    }
    Stats.TotalUniqueExprNodes = Seen.size();
    return Stats;
  }

private:
  std::size_t collectImpl(const expr_ref_t &Expr) {
    if (!Expr) {
      return 0;
    }

    const auto *Key = static_cast<const void *>(Expr.get());
    auto MemoIt = DepthMemo.find(Key);
    if (MemoIt != DepthMemo.end()) {
      ++Stats.TotalSharedRefs;
      return MemoIt->second;
    }

    Seen.insert(Key);
    std::size_t Depth = 1;
    switch (Expr->K) {
    case ExprFactoryT::Kind::Zero:
    case ExprFactoryT::Kind::One:
    case ExprFactoryT::Kind::Atom:
      break;
    case ExprFactoryT::Kind::Union:
      ++Stats.TotalUnionCount;
      Depth = 1 + std::max(collectImpl(Expr->L), collectImpl(Expr->R));
      break;
    case ExprFactoryT::Kind::Concat:
      ++Stats.TotalConcatCount;
      Depth = 1 + std::max(collectImpl(Expr->L), collectImpl(Expr->R));
      break;
    case ExprFactoryT::Kind::Star:
      ++Stats.TotalStarCount;
      Depth = 1 + collectImpl(Expr->L);
      break;
    }

    DepthMemo.emplace(Key, Depth);
    return Depth;
  }

  OrderTraceMatrixStats Stats;
  std::unordered_set<const void *> Seen;
  std::unordered_map<const void *, std::size_t> DepthMemo;
};

template <typename ExprFactoryT>
OrderTraceMatrixStats collectOrderTraceMatrixStats(
    const std::vector<std::vector<typename ExprFactoryT::Ref>> &Matrix) {
  OrderTraceMatrixStatsCollector<ExprFactoryT> Collector;
  return Collector.collect(Matrix);
}

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_ORDERTRACE_H_
