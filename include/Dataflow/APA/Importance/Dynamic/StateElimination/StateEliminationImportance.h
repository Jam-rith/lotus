#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_STATEELIMINATIONIMPORTANCE_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_STATEELIMINATIONIMPORTANCE_H_

#include "Dataflow/APA/Importance/ImportancePolicy.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/EliminationMatrixStats.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/OrderTrace.h"

#include <cstddef>
#include <unordered_set>
#include <vector>

namespace elimination {

// Collects ordinary state-elimination ordering features from the current path
// expression matrix. Solver code should pass the matrix/alive set here instead
// of hand-computing scoring inputs.
template <typename ExprFactoryT> class StateEliminationImportanceCollector final {
public:
  using expr_ref_t = typename ExprFactoryT::Ref;
  using matrix_t = std::vector<std::vector<expr_ref_t>>;

  explicit StateEliminationImportanceCollector(const ExprFactoryT &Exprs)
      : Exprs(Exprs) {}

  StateEliminationImportanceFeatures collect(const matrix_t &Matrix,
                                             const std::vector<bool> &Alive,
                                             std::size_t Node) const {
    StateEliminationImportanceFeatures Features;
    Features.Candidate =
        collectEliminationCandidateStats<ExprFactoryT>(Exprs, Matrix, Alive,
                                                       Node);
    Features.MatrixNodeCount = Matrix.size();
    if (Node < Matrix.size()) {
      Features.SelfExprSize = exprSize(Matrix[Node][Node]);
      Features.HasNontrivialSelfLoop =
          !ExprFactoryT::isZero(Matrix[Node][Node]) &&
          !ExprFactoryT::isOne(Matrix[Node][Node]);
    }
    return Features;
  }

  std::size_t score(const matrix_t &Matrix, const std::vector<bool> &Alive,
                    std::size_t Node,
                    const StateEliminationImportancePolicy<std::size_t> &Policy)
      const {
    return Policy.scoreCandidate(collect(Matrix, Alive, Node));
  }

  OrderTraceRow collectOrderTraceRow(const matrix_t &Matrix,
                                     const std::vector<bool> &Alive,
                                     std::size_t Node,
                                     const OrderingFeatureNeeds &Needs) const {
    OrderTraceRow Row;
    Row.NodeIndex = Node;

    const bool NeedExprStats = Needs.needsAnyExpressionFeature();
    const bool NeedFillInStats = Needs.needsFillInFeature();
    if (NeedExprStats || NeedFillInStats) {
      Row.Candidate =
          collectEliminationCandidateStats<ExprFactoryT>(Exprs, Matrix, Alive,
                                                         Node);
      return Row;
    }

    if (Node >= Matrix.size() || Node >= Alive.size() || !Alive[Node]) {
      return Row;
    }

    for (std::size_t Other = 0; Other < Matrix.size(); ++Other) {
      if (Other >= Alive.size() || !Alive[Other] || Other == Node) {
        continue;
      }
      if (!ExprFactoryT::isZero(Matrix[Other][Node])) {
        ++Row.Candidate.AlivePredCount;
      }
      if (!ExprFactoryT::isZero(Matrix[Node][Other])) {
        ++Row.Candidate.AliveSuccCount;
      }
    }
    Row.Candidate.CrossCombinationCount =
        saturatingMul(Row.Candidate.AlivePredCount,
                      Row.Candidate.AliveSuccCount);
    return Row;
  }

private:
  static std::size_t saturatingMul(std::size_t Lhs, std::size_t Rhs) {
    const auto Max = static_cast<std::size_t>(-1);
    if (Lhs != 0 && Rhs > Max / Lhs) {
      return Max;
    }
    return Lhs * Rhs;
  }

  static std::size_t saturatingAdd(std::size_t Lhs, std::size_t Rhs) {
    const auto Max = static_cast<std::size_t>(-1);
    if (Max - Lhs < Rhs) {
      return Max;
    }
    return Lhs + Rhs;
  }

  static std::size_t exprSize(typename ExprFactoryT::Ref Expr,
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
      return saturatingAdd(static_cast<std::size_t>(1),
                           exprSize(Expr->L, Seen));
    case ExprFactoryT::Kind::Union:
    case ExprFactoryT::Kind::Concat:
      return saturatingAdd(
          static_cast<std::size_t>(1),
          saturatingAdd(exprSize(Expr->L, Seen), exprSize(Expr->R, Seen)));
    }
    return 1;
  }

  static std::size_t exprSize(typename ExprFactoryT::Ref Expr) {
    std::unordered_set<const void *> Seen;
    return exprSize(std::move(Expr), Seen);
  }

  const ExprFactoryT &Exprs;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_STATEELIMINATIONIMPORTANCE_H_
