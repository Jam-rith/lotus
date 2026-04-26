#ifndef DATAFLOW_APA_IMPORTANCE_IMPORTANCEMODEL_H_
#define DATAFLOW_APA_IMPORTANCE_IMPORTANCEMODEL_H_

#include "Dataflow/APA/Importance/Static/StaticImportanceProfile.h"

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

private:
  ImportanceModelConfig Config;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_IMPORTANCEMODEL_H_
