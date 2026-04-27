#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTPROFILE_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTPROFILE_H_

#include "Dataflow/APA/Importance/Dynamic/StateElimination/PathExpressionStats.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elimination {

// ADT dynamic statistics are stored separately from ordinary state-elimination
// statistics because ADT cost is region/tree based, not single-state based. It
// tracks F/B boundary pressure, interval sizes, and the symbolic shape of
// X/Y/L/R prefixes used by ADTSimple and ADTDelayed.

enum class ADTNodeFlag : std::uint32_t {
  Leaf = 1u << 0,
  Internal = 1u << 1,
  HasFEdges = 1u << 2,
  HasBEdges = 1u << 3,
  HasBoundaryCycle = 1u << 4,
  HasSelfLoopLeaf = 1u << 5,
};

enum class ADTVariant : std::uint8_t {
  StructureOnly = 0,
  Simple = 1,
  Delayed = 2,
};

inline std::uint32_t flagValue(ADTNodeFlag Flag) {
  return static_cast<std::uint32_t>(Flag);
}

struct ADTNodeStats final {
  std::uint32_t NodeId = 0;
  std::uint32_t ParentId = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t LeftId = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t RightId = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t Flags = 0;

  std::uint32_t Depth = 0;
  std::uint32_t RegionNodeCount = 0;
  std::uint32_t LeftRegionNodeCount = 0;
  std::uint32_t RightRegionNodeCount = 0;

  std::uint32_t FSize = 0;
  std::uint32_t BSize = 0;
  std::uint32_t BoundaryProduct = 0;

  PathExpressionStats XExprStats;
  PathExpressionStats YExprStats;
  PathExpressionStats LoopExprStats;
  PathExpressionStats LeftPrefixBeforeLeafStats;
  PathExpressionStats RightPrefixBeforeLeafStats;
  PathExpressionStats LeftPrefixStats;
  PathExpressionStats RightPrefixStats;
  PathExpressionStats LeafBaseExprStats;
  PathExpressionStats FinalLeafExprStats;

  bool hasFlag(ADTNodeFlag Flag) const {
    return (Flags & flagValue(Flag)) != 0;
  }
};

struct ADTSummary final {
  ADTVariant Variant = ADTVariant::StructureOnly;

  std::uint32_t TotalNodes = 0;
  std::uint32_t LeafCount = 0;
  std::uint32_t InternalCount = 0;
  std::uint32_t MaxDepth = 0;
  std::uint32_t TotalFEdges = 0;
  std::uint32_t TotalBEdges = 0;
  std::uint32_t MaxBoundaryProduct = 0;
  std::uint64_t SumBoundaryProduct = 0;
  std::uint32_t NodesWithBoundaryCycle = 0;

  std::uint64_t SumXUniqueNodes = 0;
  std::uint64_t SumYUniqueNodes = 0;
  std::uint64_t SumLoopUniqueNodes = 0;
  std::uint64_t SumLeftPrefixBeforeLeafUniqueNodes = 0;
  std::uint64_t SumRightPrefixBeforeLeafUniqueNodes = 0;
  std::uint64_t SumLeftPrefixUniqueNodes = 0;
  std::uint64_t SumRightPrefixUniqueNodes = 0;
  std::uint64_t SumLeafBaseUniqueNodes = 0;
  std::uint64_t SumFinalLeafUniqueNodes = 0;
};

template <typename ADTNodeT> class ADTProfile final {
public:
  using adt_node_t = ADTNodeT;

  void clear() {
    NodeStats.clear();
    Summary = ADTSummary();
  }

  void setVariant(ADTVariant Variant) { Summary.Variant = Variant; }

  void setNodeStats(const ADTNodeT *Node, ADTNodeStats Stats) {
    NodeStats[Node] = std::move(Stats);
  }

  ADTNodeStats &getOrCreateNodeStats(const ADTNodeT *Node) {
    return NodeStats[Node];
  }

  const ADTNodeStats *findNodeStats(const ADTNodeT *Node) const {
    auto It = NodeStats.find(Node);
    if (It == NodeStats.end()) {
      return nullptr;
    }
    return &It->second;
  }

  ADTNodeStats *findNodeStats(const ADTNodeT *Node) {
    auto It = NodeStats.find(Node);
    if (It == NodeStats.end()) {
      return nullptr;
    }
    return &It->second;
  }

  const std::unordered_map<const ADTNodeT *, ADTNodeStats> &nodes() const {
    return NodeStats;
  }

  void setSummary(ADTSummary NewSummary) { Summary = NewSummary; }
  const ADTSummary &summary() const { return Summary; }
  bool empty() const { return NodeStats.empty(); }

  // Expression stats may be recorded after the structure profile is built.
  // Recompute only the expression aggregate fields and keep tree/F-B counters
  // untouched.
  void recomputeExpressionSummary() {
    Summary.SumXUniqueNodes = 0;
    Summary.SumYUniqueNodes = 0;
    Summary.SumLoopUniqueNodes = 0;
    Summary.SumLeftPrefixBeforeLeafUniqueNodes = 0;
    Summary.SumRightPrefixBeforeLeafUniqueNodes = 0;
    Summary.SumLeftPrefixUniqueNodes = 0;
    Summary.SumRightPrefixUniqueNodes = 0;
    Summary.SumLeafBaseUniqueNodes = 0;
    Summary.SumFinalLeafUniqueNodes = 0;

    for (const auto &Entry : NodeStats) {
      const auto &Stats = Entry.second;
      Summary.SumXUniqueNodes += Stats.XExprStats.UniqueNodeCount;
      Summary.SumYUniqueNodes += Stats.YExprStats.UniqueNodeCount;
      Summary.SumLoopUniqueNodes += Stats.LoopExprStats.UniqueNodeCount;
      Summary.SumLeftPrefixBeforeLeafUniqueNodes +=
          Stats.LeftPrefixBeforeLeafStats.UniqueNodeCount;
      Summary.SumRightPrefixBeforeLeafUniqueNodes +=
          Stats.RightPrefixBeforeLeafStats.UniqueNodeCount;
      Summary.SumLeftPrefixUniqueNodes +=
          Stats.LeftPrefixStats.UniqueNodeCount;
      Summary.SumRightPrefixUniqueNodes +=
          Stats.RightPrefixStats.UniqueNodeCount;
      Summary.SumLeafBaseUniqueNodes +=
          Stats.LeafBaseExprStats.UniqueNodeCount;
      Summary.SumFinalLeafUniqueNodes +=
          Stats.FinalLeafExprStats.UniqueNodeCount;
    }
  }

private:
  std::unordered_map<const ADTNodeT *, ADTNodeStats> NodeStats;
  ADTSummary Summary;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTPROFILE_H_
