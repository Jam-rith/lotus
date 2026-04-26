#ifndef DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCEPROFILE_H_
#define DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCEPROFILE_H_

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elimination {

// Static node facts are computed once from the input CFG and optional
// reducibility/dominator metadata. They do not change during state elimination.
//
// This file intentionally contains data containers only. Code that computes
// these fields belongs in StaticImportanceBuilder.h.
template <typename NodeT> struct StaticNodeImportance final {
  // Local CFG shape.
  std::size_t InDegree = 0;
  std::size_t OutDegree = 0;
  std::size_t InOutProduct = 0;

  // Basic structural roles.
  bool IsEntry = false;
  bool IsExit = false;
  bool IsBranch = false;
  bool IsJoin = false;
  bool IsLinear = false;
  bool IsCompressibleLinear = false;
  bool HasSelfLoop = false;

  // Loop and back-edge roles. These are only precise when the problem can
  // classify back edges.
  bool InLoop = false;
  std::size_t BackEdgeInCount = 0;
  std::size_t BackEdgeOutCount = 0;
  bool IsLoopHeader = false;
  bool IsLoopLatch = false;

  // Boundary facts mark nodes that connect regions or control-flow structure.
  bool IsBoundary = false;
  std::size_t BoundaryScore = 0;

  // Dominator facts are filled only when the problem provides idom/dominates.
  bool HasDominatorInfo = false;
  std::size_t DominatorChildren = 0;
  std::size_t DominatedCount = 0;
};

template <typename NodeT> struct StaticEdgeImportance final {
  NodeT Src{};
  NodeT Dst{};
  bool IsBackEdge = false;
};

struct StaticImportanceSummary final {
  std::size_t Nodes = 0;
  std::size_t Edges = 0;

  std::size_t EntryNodes = 0;
  std::size_t ExitNodes = 0;
  std::size_t BranchNodes = 0;
  std::size_t JoinNodes = 0;
  std::size_t LinearNodes = 0;
  std::size_t CompressibleLinearNodes = 0;

  std::size_t SelfLoopNodes = 0;
  std::size_t BackEdges = 0;
  std::size_t LoopTouchedNodes = 0;
  std::size_t LoopHeaders = 0;
  std::size_t LoopLatches = 0;
  std::size_t BoundaryNodes = 0;

  std::size_t MaxInDegree = 0;
  std::size_t MaxOutDegree = 0;
  std::size_t MaxInOutProduct = 0;
  std::size_t MaxDominatorChildren = 0;
  std::size_t MaxDominatedCount = 0;
};

template <typename NodeT> class StaticImportanceProfile final {
public:
  using node_t = NodeT;
  using node_importance_t = StaticNodeImportance<NodeT>;
  using edge_importance_t = StaticEdgeImportance<NodeT>;

  void setNodeImportance(const NodeT &Node, node_importance_t Importance) {
    NodeImportance[Node] = std::move(Importance);
  }

  node_importance_t &getOrCreateNodeImportance(const NodeT &Node) {
    return NodeImportance[Node];
  }

  const node_importance_t *findNodeImportance(const NodeT &Node) const {
    auto It = NodeImportance.find(Node);
    if (It == NodeImportance.end()) {
      return nullptr;
    }
    return &It->second;
  }

  void addEdgeImportance(edge_importance_t Importance) {
    EdgeImportance.push_back(std::move(Importance));
  }

  const std::unordered_map<NodeT, node_importance_t> &nodes() const {
    return NodeImportance;
  }

  const std::vector<edge_importance_t> &edges() const {
    return EdgeImportance;
  }

  void setSummary(StaticImportanceSummary NewSummary) {
    Summary = NewSummary;
  }

  const StaticImportanceSummary &summary() const { return Summary; }

  bool empty() const { return NodeImportance.empty(); }

private:
  std::unordered_map<NodeT, node_importance_t> NodeImportance;
  std::vector<edge_importance_t> EdgeImportance;
  StaticImportanceSummary Summary;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCEPROFILE_H_
