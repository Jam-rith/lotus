#ifndef DATAFLOW_APA_IMPORTANCE_DEMAND_DEMANDPROFILE_H_
#define DATAFLOW_APA_IMPORTANCE_DEMAND_DEMANDPROFILE_H_

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elimination {

// Demand importance records compact query relevance. It stores only per-node
// flags/distances and region summaries; it does not store full slices, paths,
// or per-query distance matrices.

enum class DemandNodeFlag : std::uint32_t {
  QueryTarget = 1u << 0,
  DemandRelevant = 1u << 1,
  ShouldMaterialize = 1u << 2,
  CanDelay = 1u << 3,
  InRegion = 1u << 4,
};

enum class DemandReason : std::uint16_t {
  QueryTarget = 1u << 0,
  OnDemandPath = 1u << 1,
  Boundary = 1u << 2,
  Entry = 1u << 3,
  Exit = 1u << 4,
  Unknown = 1u << 5,
};

inline std::uint32_t flagValue(DemandNodeFlag Flag) {
  return static_cast<std::uint32_t>(Flag);
}

inline std::uint16_t reasonValue(DemandReason Reason) {
  return static_cast<std::uint16_t>(Reason);
}

template <typename NodeT> struct DemandNodeInfo final {
  std::uint32_t Flags = 0;
  std::uint16_t Reasons = 0;
  std::uint32_t DemandDistance = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t RegionId = std::numeric_limits<std::uint32_t>::max();

  bool hasFlag(DemandNodeFlag Flag) const {
    return (Flags & flagValue(Flag)) != 0;
  }

  bool hasReason(DemandReason Reason) const {
    return (Reasons & reasonValue(Reason)) != 0;
  }
};

template <typename NodeT> struct DemandRegionInfo final {
  std::uint32_t RegionId = 0;
  NodeT SeedNode{};
  std::uint32_t QueryTargetCount = 0;
  std::uint32_t NodeCount = 0;
  std::uint32_t EstimatedSkippedNodes = 0;
};

struct DemandSummary final {
  std::uint32_t TotalNodes = 0;
  std::uint32_t TotalEdges = 0;
  std::uint32_t QueryTargets = 0;
  std::uint32_t DemandRelevantNodes = 0;
  std::uint32_t ShouldMaterializeNodes = 0;
  std::uint32_t CanDelayNodes = 0;
  std::uint32_t DemandRegions = 0;
  std::uint32_t EstimatedSkippedNodes = 0;
  double DemandReductionRatio = 0.0;
  double AverageDemandDistance = 0.0;
  std::uint32_t MaxDemandDistance = 0;
};

template <typename NodeT> class DemandProfile final {
public:
  using node_t = NodeT;
  using node_info_t = DemandNodeInfo<NodeT>;
  using region_info_t = DemandRegionInfo<NodeT>;

  void setNodeInfo(const NodeT &Node, node_info_t Info) {
    NodeInfo[Node] = std::move(Info);
  }

  node_info_t &getOrCreateNodeInfo(const NodeT &Node) { return NodeInfo[Node]; }

  const node_info_t *findNodeInfo(const NodeT &Node) const {
    auto It = NodeInfo.find(Node);
    if (It == NodeInfo.end()) {
      return nullptr;
    }
    return &It->second;
  }

  node_info_t *findNodeInfo(const NodeT &Node) {
    auto It = NodeInfo.find(Node);
    if (It == NodeInfo.end()) {
      return nullptr;
    }
    return &It->second;
  }

  void addRegion(region_info_t Region) {
    Regions.push_back(std::move(Region));
  }

  const std::unordered_map<NodeT, node_info_t> &nodes() const {
    return NodeInfo;
  }

  std::unordered_map<NodeT, node_info_t> &nodes() { return NodeInfo; }

  const std::vector<region_info_t> &regions() const { return Regions; }

  void setSummary(DemandSummary NewSummary) { Summary = NewSummary; }
  const DemandSummary &summary() const { return Summary; }

private:
  std::unordered_map<NodeT, node_info_t> NodeInfo;
  std::vector<region_info_t> Regions;
  DemandSummary Summary;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DEMAND_DEMANDPROFILE_H_
