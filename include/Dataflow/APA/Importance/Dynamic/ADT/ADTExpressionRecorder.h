#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTEXPRESSIONRECORDER_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTEXPRESSIONRECORDER_H_

#include "Dataflow/APA/Importance/Dynamic/ADT/ADTProfile.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/PathExpressionStats.h"

namespace elimination {

// Records the expression-level part of an ADT profile. The solver already
// constructs X, Y, L, and R-prefix expressions; this helper only summarizes
// their DAG shape and never evaluates facts.
template <typename ADTNodeT, typename ExprFactoryT>
class ADTExpressionRecorder final {
public:
  using profile_t = ADTProfile<ADTNodeT>;
  using expr_ref_t = typename ExprFactoryT::Ref;

  explicit ADTExpressionRecorder(profile_t &Profile) : Profile(Profile) {}

  void recordLeafBase(const ADTNodeT *Leaf, const expr_ref_t &BaseExpr,
                      bool HasSelfLoop) {
    auto &Stats = Profile.getOrCreateNodeStats(Leaf);
    Stats.Flags |= flagValue(ADTNodeFlag::Leaf);
    if (HasSelfLoop) {
      Stats.Flags |= flagValue(ADTNodeFlag::HasSelfLoopLeaf);
    }
    Stats.LeafBaseExprStats =
        collectPathExpressionStats<ExprFactoryT>(BaseExpr);
  }

  void recordComposition(const ADTNodeT *Node, const expr_ref_t &X,
                         const expr_ref_t &Y, const expr_ref_t &Loop,
                         const expr_ref_t &LeftPrefixBeforeLeaf,
                         const expr_ref_t &RightPrefixBeforeLeaf,
                         const expr_ref_t &LeftPrefix,
                         const expr_ref_t &RightPrefix) {
    auto &Stats = Profile.getOrCreateNodeStats(Node);
    Stats.Flags |= flagValue(ADTNodeFlag::Internal);
    Stats.XExprStats = collectPathExpressionStats<ExprFactoryT>(X);
    Stats.YExprStats = collectPathExpressionStats<ExprFactoryT>(Y);
    Stats.LoopExprStats = collectPathExpressionStats<ExprFactoryT>(Loop);
    Stats.LeftPrefixBeforeLeafStats =
        collectPathExpressionStats<ExprFactoryT>(LeftPrefixBeforeLeaf);
    Stats.RightPrefixBeforeLeafStats =
        collectPathExpressionStats<ExprFactoryT>(RightPrefixBeforeLeaf);
    Stats.LeftPrefixStats =
        collectPathExpressionStats<ExprFactoryT>(LeftPrefix);
    Stats.RightPrefixStats =
        collectPathExpressionStats<ExprFactoryT>(RightPrefix);
  }

  void recordFinalLeaf(const ADTNodeT *Leaf, const expr_ref_t &FinalExpr) {
    auto &Stats = Profile.getOrCreateNodeStats(Leaf);
    Stats.Flags |= flagValue(ADTNodeFlag::Leaf);
    Stats.FinalLeafExprStats =
        collectPathExpressionStats<ExprFactoryT>(FinalExpr);
  }

  void finalize() { Profile.recomputeExpressionSummary(); }

private:
  profile_t &Profile;
};

template <typename ExprFactoryT, typename ADTNodeT>
void recordADTLeafBaseExpr(ADTProfile<ADTNodeT> &Profile,
                           const ADTNodeT *Leaf,
                           typename ExprFactoryT::Ref BaseExpr,
                           bool HasSelfLoop) {
  ADTExpressionRecorder<ADTNodeT, ExprFactoryT> Recorder(Profile);
  Recorder.recordLeafBase(Leaf, BaseExpr, HasSelfLoop);
}

template <typename ExprFactoryT, typename ADTNodeT>
void recordADTCompositionExpr(ADTProfile<ADTNodeT> &Profile,
                              const ADTNodeT *Node,
                              typename ExprFactoryT::Ref X,
                              typename ExprFactoryT::Ref Y,
                              typename ExprFactoryT::Ref Loop,
                              typename ExprFactoryT::Ref LeftPrefixBeforeLeaf,
                              typename ExprFactoryT::Ref RightPrefixBeforeLeaf,
                              typename ExprFactoryT::Ref LeftPrefix,
                              typename ExprFactoryT::Ref RightPrefix) {
  ADTExpressionRecorder<ADTNodeT, ExprFactoryT> Recorder(Profile);
  Recorder.recordComposition(Node, X, Y, Loop, LeftPrefixBeforeLeaf,
                             RightPrefixBeforeLeaf, LeftPrefix, RightPrefix);
}

template <typename ExprFactoryT, typename ADTNodeT>
void recordADTFinalLeafExpr(ADTProfile<ADTNodeT> &Profile,
                            const ADTNodeT *Leaf,
                            typename ExprFactoryT::Ref FinalExpr) {
  ADTExpressionRecorder<ADTNodeT, ExprFactoryT> Recorder(Profile);
  Recorder.recordFinalLeaf(Leaf, FinalExpr);
}

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTEXPRESSIONRECORDER_H_
