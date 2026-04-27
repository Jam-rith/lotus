#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_ELIMINATIONMATRIXSTATS_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_ELIMINATIONMATRIXSTATS_H_

#include "Dataflow/APA/Importance/Dynamic/StateElimination/PathExpressionStats.h"

#include <cstddef>
#include <vector>

namespace elimination {

// Dynamic per-candidate statistics that require the current elimination matrix.
// These cannot be represented by the static CFG profile alone.
struct EliminationCandidateStats final {
  std::size_t AlivePredCount = 0;
  std::size_t AliveSuccCount = 0;
  std::size_t CrossCombinationCount = 0;

  // Fill-in minimization: pairs (i,j) where i->k and k->j exist but M[i][j] is
  // still zero. Lower is usually better.
  std::size_t FillInCount = 0;
  // ExistingTargetCount is the complement inside candidate cross-combinations.
  // It estimates how often elimination updates an already-present summary.
  std::size_t ExistingTargetCount = 0;

  // Reuse-aware signals. ExistingViaConcatCount is a conservative proxy: it
  // counts cross-combinations whose produced via expression is already the same
  // pointer as M[i][j] after canonical factory construction.
  std::size_t ExistingViaConcatCount = 0;
  std::size_t SharedInputRefCount = 0;

  PathExpressionStats SelfExprStats;
  PathExpressionStats IncomingExprStats;
  PathExpressionStats OutgoingExprStats;
};

template <typename ExprFactoryT> class EliminationMatrixStatsCollector final {
public:
  using expr_ref_t = typename ExprFactoryT::Ref;
  using matrix_t = std::vector<std::vector<expr_ref_t>>;

  EliminationCandidateStats collect(const ExprFactoryT &Exprs,
                                    const matrix_t &Matrix,
                                    const std::vector<bool> &Alive,
                                    std::size_t Node) const {
    EliminationCandidateStats Stats;
    if (Node >= Matrix.size() || Node >= Alive.size() || !Alive[Node]) {
      return Stats;
    }

    std::vector<std::size_t> Preds;
    std::vector<std::size_t> Succs;
    for (std::size_t Other = 0; Other < Matrix.size(); ++Other) {
      if (Other >= Alive.size() || !Alive[Other] || Other == Node) {
        continue;
      }
      if (!ExprFactoryT::isZero(Matrix[Other][Node])) {
        Preds.push_back(Other);
      }
      if (!ExprFactoryT::isZero(Matrix[Node][Other])) {
        Succs.push_back(Other);
      }
    }

    Stats.AlivePredCount = Preds.size();
    Stats.AliveSuccCount = Succs.size();
    Stats.CrossCombinationCount = Preds.size() * Succs.size();
    Stats.SelfExprStats = collectPathExpressionStats<ExprFactoryT>(Matrix[Node][Node]);
    Stats.IncomingExprStats = collectExpressionListStats(Matrix, Preds, Node,
                                                         /*ColumnMode=*/true);
    Stats.OutgoingExprStats = collectExpressionListStats(Matrix, Succs, Node,
                                                         /*ColumnMode=*/false);

    const auto KStar = Exprs.star(Matrix[Node][Node]);
    for (const auto Pred : Preds) {
      for (const auto Succ : Succs) {
        if (ExprFactoryT::isZero(Matrix[Pred][Succ])) {
          ++Stats.FillInCount;
        } else {
          ++Stats.ExistingTargetCount;
        }

        auto Via = Exprs.concat(Matrix[Pred][Node], KStar);
        Via = Exprs.concat(Via, Matrix[Node][Succ]);
        if (Via == Matrix[Pred][Succ]) {
          ++Stats.ExistingViaConcatCount;
        }
        if (Matrix[Pred][Node] == Matrix[Node][Succ] ||
            Matrix[Pred][Node] == Matrix[Node][Node] ||
            Matrix[Node][Succ] == Matrix[Node][Node]) {
          ++Stats.SharedInputRefCount;
        }
      }
    }

    return Stats;
  }

private:
  static PathExpressionStats
  mergeStats(const PathExpressionStats &Lhs, const PathExpressionStats &Rhs) {
    PathExpressionStats Out;
    Out.UniqueNodeCount = Lhs.UniqueNodeCount + Rhs.UniqueNodeCount;
    Out.SharedRefCount = Lhs.SharedRefCount + Rhs.SharedRefCount;
    Out.MaxDepth = Lhs.MaxDepth > Rhs.MaxDepth ? Lhs.MaxDepth : Rhs.MaxDepth;
    Out.ZeroCount = Lhs.ZeroCount + Rhs.ZeroCount;
    Out.OneCount = Lhs.OneCount + Rhs.OneCount;
    Out.AtomCount = Lhs.AtomCount + Rhs.AtomCount;
    Out.UnionCount = Lhs.UnionCount + Rhs.UnionCount;
    Out.ConcatCount = Lhs.ConcatCount + Rhs.ConcatCount;
    Out.StarCount = Lhs.StarCount + Rhs.StarCount;
    Out.PlusCount = Lhs.PlusCount + Rhs.PlusCount;
    return Out;
  }

  static PathExpressionStats collectExpressionListStats(
      const matrix_t &Matrix, const std::vector<std::size_t> &Indexes,
      std::size_t Node, bool ColumnMode) {
    PathExpressionStats Total;
    for (const auto Index : Indexes) {
      const auto &Expr = ColumnMode ? Matrix[Index][Node] : Matrix[Node][Index];
      Total = mergeStats(Total, collectPathExpressionStats<ExprFactoryT>(Expr));
    }
    return Total;
  }
};

template <typename ExprFactoryT>
EliminationCandidateStats collectEliminationCandidateStats(
    const ExprFactoryT &Exprs,
    const std::vector<std::vector<typename ExprFactoryT::Ref>> &Matrix,
    const std::vector<bool> &Alive, std::size_t Node) {
  EliminationMatrixStatsCollector<ExprFactoryT> Collector;
  return Collector.collect(Exprs, Matrix, Alive, Node);
}

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_ELIMINATIONMATRIXSTATS_H_
