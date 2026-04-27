#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTBUILDER_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTBUILDER_H_

#include "Dataflow/APA/Importance/Dynamic/ADT/ADTProfile.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace elimination {

// Builds the structure-only ADT profile. This records facts that are already
// present after prepareADT(): tree shape, interval sizes, and F/B boundary
// pressure. It does not inspect path expressions.
template <typename ADTNodeT> class ADTBuilder final {
public:
  using profile_t = ADTProfile<ADTNodeT>;

  profile_t build(const ADTNodeT *Root,
                  ADTVariant Variant = ADTVariant::StructureOnly) const {
    profile_t Profile;
    Profile.setVariant(Variant);
    if (!Root) {
      return Profile;
    }

    ADTSummary Summary;
    Summary.Variant = Variant;
    std::uint32_t NextNodeId = 0;
    visit(Root, std::numeric_limits<std::uint32_t>::max(), 0, Profile,
          Summary, NextNodeId);
    Profile.setSummary(Summary);
    return Profile;
  }

private:
  static std::uint32_t regionNodeCount(const ADTNodeT *Node) {
    if (!Node || Node->MaxPos < Node->MinPos) {
      return 0;
    }
    return static_cast<std::uint32_t>(Node->MaxPos - Node->MinPos + 1);
  }

  static std::uint32_t safeBoundaryProduct(std::size_t FSize,
                                           std::size_t BSize) {
    const auto Max = std::numeric_limits<std::uint32_t>::max();
    if (FSize != 0 && BSize > Max / FSize) {
      return Max;
    }
    return static_cast<std::uint32_t>(FSize * BSize);
  }

  static std::uint32_t visit(const ADTNodeT *Node, std::uint32_t ParentId,
                             std::uint32_t Depth, profile_t &Profile,
                             ADTSummary &Summary,
                             std::uint32_t &NextNodeId) {
    const auto NodeId = NextNodeId++;

    ADTNodeStats Stats;
    Stats.NodeId = NodeId;
    Stats.ParentId = ParentId;
    Stats.Depth = Depth;
    Stats.RegionNodeCount = regionNodeCount(Node);

    ++Summary.TotalNodes;
    Summary.MaxDepth = std::max(Summary.MaxDepth, Stats.Depth);

    if (Node->Leaf) {
      Stats.Flags |= flagValue(ADTNodeFlag::Leaf);
      ++Summary.LeafCount;
    } else {
      Stats.Flags |= flagValue(ADTNodeFlag::Internal);
      ++Summary.InternalCount;

      Stats.FSize = static_cast<std::uint32_t>(Node->F.size());
      Stats.BSize = static_cast<std::uint32_t>(Node->B.size());
      Stats.BoundaryProduct = safeBoundaryProduct(Node->F.size(),
                                                  Node->B.size());
      Stats.LeftRegionNodeCount = regionNodeCount(Node->Left);
      Stats.RightRegionNodeCount = regionNodeCount(Node->Right);

      if (!Node->F.empty()) {
        Stats.Flags |= flagValue(ADTNodeFlag::HasFEdges);
      }
      if (!Node->B.empty()) {
        Stats.Flags |= flagValue(ADTNodeFlag::HasBEdges);
      }
      if (!Node->F.empty() && !Node->B.empty()) {
        Stats.Flags |= flagValue(ADTNodeFlag::HasBoundaryCycle);
        ++Summary.NodesWithBoundaryCycle;
      }

      Summary.TotalFEdges += Stats.FSize;
      Summary.TotalBEdges += Stats.BSize;
      Summary.MaxBoundaryProduct =
          std::max(Summary.MaxBoundaryProduct, Stats.BoundaryProduct);
      Summary.SumBoundaryProduct += Stats.BoundaryProduct;
    }

    if (Node->Left) {
      Stats.LeftId = visit(Node->Left, NodeId, Depth + 1, Profile, Summary,
                           NextNodeId);
    }
    if (Node->Right) {
      Stats.RightId = visit(Node->Right, NodeId, Depth + 1, Profile, Summary,
                              NextNodeId);
    }
    Profile.setNodeStats(Node, Stats);
    return NodeId;
  }
};

template <typename ADTNodeT>
ADTProfile<ADTNodeT>
buildADTProfile(const ADTNodeT *Root,
                ADTVariant Variant = ADTVariant::StructureOnly) {
  ADTBuilder<ADTNodeT> Builder;
  return Builder.build(Root, Variant);
}

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTBUILDER_H_
