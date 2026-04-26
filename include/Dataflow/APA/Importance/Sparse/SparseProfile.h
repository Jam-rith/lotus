#ifndef DATAFLOW_APA_IMPORTANCE_SPARSE_SPARSEPROFILE_H_
#define DATAFLOW_APA_IMPORTANCE_SPARSE_SPARSEPROFILE_H_

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elimination {

// Sparse importance records compact compression potential. It deliberately
// avoids storing strings, full node lists, or transfer summaries.

enum class SparseNodeFlag : std::uint32_t {
  Candidate = 1u << 0,
  Compressible = 1u << 1,
  Linear = 1u << 2,
  InRegion = 1u << 3,
  MustPreserve = 1u << 4,
};

enum class SparsePreserveReason : std::uint16_t {
  Entry = 1u << 0,
  Exit = 1u << 1,
  Branch = 1u << 2,
  Join = 1u << 3,
  LoopHeader = 1u << 4,
  LoopLatch = 1u << 5,
  SelfLoop = 1u << 6,
  Boundary = 1u << 7,
};

enum class SparseRegionFlag : std::uint16_t {
  LinearChain = 1u << 0,
  Compressible = 1u << 1,
};

inline std::uint32_t flagValue(SparseNodeFlag Flag) {
  return static_cast<std::uint32_t>(Flag);
}

inline std::uint16_t reasonValue(SparsePreserveReason Reason) {
  return static_cast<std::uint16_t>(Reason);
}

inline std::uint16_t flagValue(SparseRegionFlag Flag) {
  return static_cast<std::uint16_t>(Flag);
}

template <typename NodeT> struct SparseNodeInfo final {
  std::uint32_t Flags = 0;
  std::uint16_t PreserveReasons = 0;
  std::uint32_t RegionId = std::numeric_limits<std::uint32_t>::max();

  bool hasFlag(SparseNodeFlag Flag) const {
    return (Flags & flagValue(Flag)) != 0;
  }

  bool hasPreserveReason(SparsePreserveReason Reason) const {
    return (PreserveReasons & reasonValue(Reason)) != 0;
  }
};

template <typename NodeT> struct SparseRegionInfo final {
  std::uint32_t RegionId = 0;
  NodeT EntryNode{};
  NodeT ExitNode{};

  std::uint32_t NodeCount = 0;
  std::uint32_t CompressibleNodeCount = 0;
  std::uint32_t EstimatedRemovedNodes = 0;
  std::uint16_t Flags = 0;

  bool hasFlag(SparseRegionFlag Flag) const {
    return (Flags & flagValue(Flag)) != 0;
  }
};

struct SparseSummary final {
  std::uint32_t TotalNodes = 0;
  std::uint32_t CandidateNodes = 0;
  std::uint32_t CompressibleNodes = 0;
  std::uint32_t MustPreserveNodes = 0;
  std::uint32_t Regions = 0;
  std::uint32_t CompressibleRegions = 0;
  std::uint32_t EstimatedRemovedNodes = 0;
  double CompressionRatio = 0.0;
};

template <typename NodeT> class SparseProfile final {
public:
  using node_t = NodeT;
  using node_info_t = SparseNodeInfo<NodeT>;
  using region_info_t = SparseRegionInfo<NodeT>;

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

  const std::vector<region_info_t> &regions() const { return Regions; }

  void setSummary(SparseSummary NewSummary) { Summary = NewSummary; }
  const SparseSummary &summary() const { return Summary; }

private:
  std::unordered_map<NodeT, node_info_t> NodeInfo;
  std::vector<region_info_t> Regions;
  SparseSummary Summary;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_SPARSE_SPARSEPROFILE_H_
