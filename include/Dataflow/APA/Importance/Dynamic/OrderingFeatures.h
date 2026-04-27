#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ORDERING_ORDERINGFEATURES_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ORDERING_ORDERINGFEATURES_H_

#include <cstddef>
#include <string>
#include <unordered_set>

namespace elimination {

// Solver-independent feature vector used by learned ordering policies.
//
// State elimination, ADT, and ADTDelayed expose different "candidates"
// (matrix states vs. ADT composition nodes), but the ordering model should see
// a stable feature vocabulary. A solver may leave unsupported fields as zero;
// LinearOrderingModel::featureNeeds() lets each solver avoid computing fields
// that are not used by the loaded model.
struct OrderingFeatureVector final {
  double Score = 0.0;
  double AlivePredCount = 0.0;
  double AliveSuccCount = 0.0;
  double PredSuccProduct = 0.0;
  double CrossCombinationCount = 0.0;
  double FillInCount = 0.0;
  double ExistingTargetCount = 0.0;
  double ExistingViaConcatCount = 0.0;
  double SharedInputRefCount = 0.0;

  double SelfUniqueNodes = 0.0;
  double SelfUnionCount = 0.0;
  double SelfConcatCount = 0.0;
  double SelfStarCount = 0.0;
  double SelfMaxDepth = 0.0;

  double IncomingUniqueNodes = 0.0;
  double IncomingUnionCount = 0.0;
  double IncomingConcatCount = 0.0;
  double IncomingStarCount = 0.0;
  double IncomingMaxDepth = 0.0;

  double OutgoingUniqueNodes = 0.0;
  double OutgoingUnionCount = 0.0;
  double OutgoingConcatCount = 0.0;
  double OutgoingStarCount = 0.0;
  double OutgoingMaxDepth = 0.0;

  double MatrixTotalUniqueExprNodesBefore = 0.0;
  double MatrixTotalSharedRefsBefore = 0.0;
  double MatrixTotalConcatCountBefore = 0.0;
  double MatrixTotalUnionCountBefore = 0.0;
  double MatrixTotalStarCountBefore = 0.0;
  double MatrixMaxDepthBefore = 0.0;
};

struct OrderingFeatureNeeds final {
  std::unordered_set<std::string> UsedFeatures;

  bool needsFeature(const std::string &Name) const {
    return UsedFeatures.find(Name) != UsedFeatures.end();
  }

  bool needsAnyMatrixFeature() const {
    for (const auto &Name : UsedFeatures) {
      if (Name.find("matrix_") == 0) {
        return true;
      }
    }
    return false;
  }

  bool needsAnyExpressionFeature() const {
    for (const auto &Name : UsedFeatures) {
      if (Name.find("self_") == 0 || Name.find("incoming_") == 0 ||
          Name.find("outgoing_") == 0) {
        return true;
      }
    }
    return false;
  }

  bool needsFillInFeature() const {
    return needsFeature("fill_in_count") ||
           needsFeature("existing_target_count") ||
           needsFeature("shared_input_ref_count");
  }
};

inline double orderingFeatureValue(const OrderingFeatureVector &Features,
                                   const std::string &Name) {
  if (Name == "score")
    return Features.Score;
  if (Name == "alive_pred_count")
    return Features.AlivePredCount;
  if (Name == "alive_succ_count")
    return Features.AliveSuccCount;
  if (Name == "pred_succ_product")
    return Features.PredSuccProduct;
  if (Name == "cross_combination_count")
    return Features.CrossCombinationCount;
  if (Name == "fill_in_count")
    return Features.FillInCount;
  if (Name == "existing_target_count")
    return Features.ExistingTargetCount;
  if (Name == "existing_via_concat_count")
    return Features.ExistingViaConcatCount;
  if (Name == "shared_input_ref_count")
    return Features.SharedInputRefCount;

  if (Name == "self_unique_nodes")
    return Features.SelfUniqueNodes;
  if (Name == "self_union_count")
    return Features.SelfUnionCount;
  if (Name == "self_concat_count")
    return Features.SelfConcatCount;
  if (Name == "self_star_count")
    return Features.SelfStarCount;
  if (Name == "self_max_depth")
    return Features.SelfMaxDepth;

  if (Name == "incoming_unique_nodes")
    return Features.IncomingUniqueNodes;
  if (Name == "incoming_union_count")
    return Features.IncomingUnionCount;
  if (Name == "incoming_concat_count")
    return Features.IncomingConcatCount;
  if (Name == "incoming_star_count")
    return Features.IncomingStarCount;
  if (Name == "incoming_max_depth")
    return Features.IncomingMaxDepth;

  if (Name == "outgoing_unique_nodes")
    return Features.OutgoingUniqueNodes;
  if (Name == "outgoing_union_count")
    return Features.OutgoingUnionCount;
  if (Name == "outgoing_concat_count")
    return Features.OutgoingConcatCount;
  if (Name == "outgoing_star_count")
    return Features.OutgoingStarCount;
  if (Name == "outgoing_max_depth")
    return Features.OutgoingMaxDepth;

  if (Name == "matrix_total_unique_expr_nodes_before")
    return Features.MatrixTotalUniqueExprNodesBefore;
  if (Name == "matrix_total_shared_refs_before")
    return Features.MatrixTotalSharedRefsBefore;
  if (Name == "matrix_total_concat_count_before")
    return Features.MatrixTotalConcatCountBefore;
  if (Name == "matrix_total_union_count_before")
    return Features.MatrixTotalUnionCountBefore;
  if (Name == "matrix_total_star_count_before")
    return Features.MatrixTotalStarCountBefore;
  if (Name == "matrix_max_depth_before")
    return Features.MatrixMaxDepthBefore;
  return 0.0;
}

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_ORDERING_ORDERINGFEATURES_H_
