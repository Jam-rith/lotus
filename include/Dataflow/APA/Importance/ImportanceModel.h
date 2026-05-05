#ifndef DATAFLOW_APA_IMPORTANCE_IMPORTANCEMODEL_H_
#define DATAFLOW_APA_IMPORTANCE_IMPORTANCEMODEL_H_

#include "Dataflow/APA/Importance/Dynamic/StateElimination/EliminationMatrixStats.h"
#include "Dataflow/APA/Importance/Static/StaticImportanceProfile.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace elimination {

// Separate scores are kept because "important" has different meanings for
// ordering, sparse compression, demand-driven solving, and incremental update.
struct ImportanceScores final {
  double EliminationCost = 0.0;
  double StarRisk = 0.0;
  double PreserveScore = 0.0;
  double CompressibleScore = 0.0;
  double DemandRelevance = 0.0;
  double UpdateWeight = 0.0;
};

struct ImportanceModelConfig final {
  double EliminationCostWeight = 1.0;
  double StarRiskWeight = 1.0;
  double PreserveWeight = 1.0;
  double CompressibleWeight = 1.0;
  double DemandWeight = 1.0;
  double UpdateWeight = 1.0;
};

// Solver-independent score input for ordinary state elimination. The solver is
// responsible for collecting facts from its current matrix; this model owns the
// equations that turn those facts into costs.
struct StateEliminationImportanceFeatures final {
  EliminationCandidateStats Candidate;
  StaticNodeImportance<std::size_t> Static;
  std::size_t SelfExprSize = 0;
  std::size_t MatrixNodeCount = 0;
  bool HasNontrivialSelfLoop = false;
};

template <typename NodeT> class ImportanceModel {
public:
  using node_t = NodeT;
  using profile_t = StaticImportanceProfile<NodeT>;
  using node_importance_t = StaticNodeImportance<NodeT>;

  explicit ImportanceModel(ImportanceModelConfig Config = {})
      : Config(Config) {}

  // Stage-1 skeleton: concrete scoring equations are intentionally deferred.
  ImportanceScores scoreNode(const NodeT &Node,
                             const node_importance_t &Importance) const {
    (void)Node;
    (void)Importance;
    return ImportanceScores();
  }

  const ImportanceModelConfig &config() const { return Config; }

  std::size_t scoreStateStructural(
      const StateEliminationImportanceFeatures &Features) const {
    return saturatingAdd(
        saturatingMul(Features.Candidate.AlivePredCount,
                      Features.Candidate.AliveSuccCount),
        staticOrderingPenalty(Features));
  }

  std::size_t scoreStateExpressionAware(
      const StateEliminationImportanceFeatures &Features) const {
    return saturatingAdd(
        saturatingAdd(expressionCrossProductCost(Features),
                      Features.Candidate.FillInCount),
        staticOrderingPenalty(Features));
  }

  std::size_t scoreStateStarRisk(
      const StateEliminationImportanceFeatures &Features) const {
    auto Score = scoreStateExpressionAware(Features);
    if (Features.HasNontrivialSelfLoop) {
      Score = saturatingAdd(
          Score,
          saturatingMul(std::max<std::size_t>(1, Features.SelfExprSize),
                        Features.MatrixNodeCount));
    }
    return Score;
  }

private:
  static std::size_t saturatingAdd(std::size_t Lhs, std::size_t Rhs) {
    const auto Max = std::numeric_limits<std::size_t>::max();
    if (Max - Lhs < Rhs) {
      return Max;
    }
    return Lhs + Rhs;
  }

  static std::size_t saturatingMul(std::size_t Lhs, std::size_t Rhs) {
    const auto Max = std::numeric_limits<std::size_t>::max();
    if (Lhs != 0 && Rhs > Max / Lhs) {
      return Max;
    }
    return Lhs * Rhs;
  }

  static std::size_t expressionWeight(const PathExpressionStats &Stats) {
    return saturatingAdd(
        std::max<std::size_t>(1, Stats.UniqueNodeCount),
        saturatingAdd(Stats.UnionCount,
                      saturatingAdd(Stats.ConcatCount,
                                    saturatingMul(Stats.StarCount, 2))));
  }

  static std::size_t expressionCrossProductCost(
      const StateEliminationImportanceFeatures &Features) {
    const auto Crosses = Features.Candidate.CrossCombinationCount;
    if (Crosses == 0) {
      return 0;
    }

    const auto Incoming =
        expressionWeight(Features.Candidate.IncomingExprStats);
    const auto Outgoing =
        expressionWeight(Features.Candidate.OutgoingExprStats);
    const auto Self =
        std::max<std::size_t>(1, expressionWeight(Features.Candidate.SelfExprStats));
    const auto InputCost = saturatingAdd(Incoming, saturatingAdd(Self, Outgoing));
    return saturatingMul(Crosses, InputCost);
  }

  static std::size_t
  staticOrderingPenalty(const StateEliminationImportanceFeatures &Features) {
    const auto &Static = Features.Static;
    std::size_t Penalty = 0;

    // Preserve interface/control nodes longer. Eliminating them early often
    // creates wider cross-products than their current alive degree suggests.
    Penalty = saturatingAdd(Penalty,
                            saturatingMul(Static.BoundaryScore,
                                          std::max<std::size_t>(
                                              1, Features.MatrixNodeCount)));
    Penalty = saturatingAdd(Penalty,
                            saturatingMul(Static.InOutProduct,
                                          static_cast<std::size_t>(2)));

    if (Static.IsEntry || Static.IsExit) {
      Penalty = saturatingAdd(Penalty, Features.MatrixNodeCount);
    }
    if (Static.IsBranch || Static.IsJoin) {
      Penalty = saturatingAdd(Penalty, Static.InDegree + Static.OutDegree);
    }
    if (Static.IsLoopHeader || Static.IsLoopLatch || Static.HasSelfLoop) {
      Penalty =
          saturatingAdd(Penalty, saturatingMul(Features.MatrixNodeCount,
                                               static_cast<std::size_t>(2)));
    }

    // Linear interior nodes are usually safe to eliminate early. Do not add a
    // static penalty for them beyond whatever dynamic score they currently have.
    if (Static.IsCompressibleLinear && Penalty > 0) {
      Penalty /= 2;
    }
    return Penalty;
  }

  ImportanceModelConfig Config;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_IMPORTANCEMODEL_H_
