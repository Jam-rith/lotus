#ifndef DATAFLOW_APA_IMPORTANCE_IMPORTANCEPROFILE_H_
#define DATAFLOW_APA_IMPORTANCE_IMPORTANCEPROFILE_H_

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elimination {

// Analysis-independent structural facts used by importance-aware APA policies.
// This layer records what a node/edge looks like; it intentionally does not
// decide how a solver should use that information.
template <typename NodeT> struct ImportanceNodeFeatures final {
  std::size_t InDegree = 0;
  std::size_t OutDegree = 0;
  std::size_t InOutProduct = 0;

  bool IsEntry = false;
  bool IsExit = false;
  bool IsBranch = false;
  bool IsJoin = false;
  bool IsLinear = false;
  bool IsCompressibleLinear = false;

  bool HasSelfLoop = false;
  bool InLoop = false;
  std::size_t LoopDepth = 0;
};

template <typename NodeT> struct ImportanceEdgeFeatures final {
  NodeT Src{};
  NodeT Dst{};
  bool IsBackEdge = false;
};

template <typename NodeT> class ImportanceProfile final {
public:
  using node_t = NodeT;
  using node_features_t = ImportanceNodeFeatures<NodeT>;
  using edge_features_t = ImportanceEdgeFeatures<NodeT>;

  void setNodeFeatures(const NodeT &Node, node_features_t Features) {
    NodeFeatures[Node] = std::move(Features);
  }

  node_features_t &getOrCreateNodeFeatures(const NodeT &Node) {
    return NodeFeatures[Node];
  }

  const node_features_t *findNodeFeatures(const NodeT &Node) const {
    auto It = NodeFeatures.find(Node);
    if (It == NodeFeatures.end()) {
      return nullptr;
    }
    return &It->second;
  }

  void addEdgeFeatures(edge_features_t Features) {
    EdgeFeatures.push_back(std::move(Features));
  }

  const std::unordered_map<NodeT, node_features_t> &nodes() const {
    return NodeFeatures;
  }

  const std::vector<edge_features_t> &edges() const { return EdgeFeatures; }

  bool empty() const { return NodeFeatures.empty(); }

private:
  std::unordered_map<NodeT, node_features_t> NodeFeatures;
  std::vector<edge_features_t> EdgeFeatures;
};

template <typename ProblemT> class ImportanceProfileBuilder {
public:
  using node_t = typename ProblemT::n_t;
  using profile_t = ImportanceProfile<node_t>;

  // Stage-1 skeleton: callers get an empty profile until concrete importance
  // collection rules are added.
  profile_t build(const ProblemT &Problem) const {
    (void)Problem;
    return profile_t();
  }
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_IMPORTANCEPROFILE_H_
