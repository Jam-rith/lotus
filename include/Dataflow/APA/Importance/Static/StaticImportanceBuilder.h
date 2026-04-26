#ifndef DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCEBUILDER_H_
#define DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCEBUILDER_H_

#include "Dataflow/APA/Importance/Static/StaticImportanceProfile.h"

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace elimination {

namespace detail {

template <typename ProblemT, typename NodeT, typename = void>
struct HasIsBackEdge : std::false_type {};

template <typename ProblemT, typename NodeT>
struct HasIsBackEdge<
    ProblemT, NodeT,
    std::void_t<decltype(std::declval<const ProblemT &>().isBackEdge(
        std::declval<NodeT>(), std::declval<NodeT>()))>> : std::true_type {};

template <typename ProblemT, typename NodeT, typename = void>
struct HasIdom : std::false_type {};

template <typename ProblemT, typename NodeT>
struct HasIdom<ProblemT, NodeT,
               std::void_t<decltype(std::declval<const ProblemT &>().idom(
                   std::declval<NodeT>()))>> : std::true_type {};

template <typename ProblemT, typename NodeT, typename = void>
struct HasDominates : std::false_type {};

template <typename ProblemT, typename NodeT>
struct HasDominates<
    ProblemT, NodeT,
    std::void_t<decltype(std::declval<const ProblemT &>().dominates(
        std::declval<NodeT>(), std::declval<NodeT>()))>> : std::true_type {};

template <typename ProblemT, typename NodeT>
bool isBackEdgeIfAvailable(const ProblemT &Problem, const NodeT &Src,
                           const NodeT &Dst) {
  if constexpr (HasIsBackEdge<ProblemT, NodeT>::value) {
    return Problem.isBackEdge(Src, Dst);
  }
  (void)Problem;
  (void)Src;
  (void)Dst;
  return false;
}

} // namespace detail

// Computes StaticImportanceProfile from a problem's immutable CFG interface.
//
// Static importance is meant to answer: "what structural role does this node
// have before elimination starts?" It should not read or update the path
// expression matrix.
template <typename ProblemT> class StaticImportanceBuilder {
public:
  using node_t = typename ProblemT::n_t;
  using profile_t = StaticImportanceProfile<node_t>;

  profile_t build(const ProblemT &Problem) const {
    profile_t Profile;
    StaticImportanceSummary Summary;

    const auto Nodes = Problem.nodes();
    std::unordered_map<node_t, std::size_t> PredCounts;
    std::unordered_map<node_t, std::size_t> SuccCounts;
    std::unordered_map<node_t, std::size_t> BackEdgeInCounts;
    std::unordered_map<node_t, std::size_t> BackEdgeOutCounts;
    std::unordered_map<node_t, std::size_t> DominatorChildCounts;
    std::unordered_map<node_t, std::size_t> DominatedCounts;
    std::unordered_set<node_t> SelfLoopNodes;
    std::unordered_set<node_t> LoopTouchedNodes;

    reserveNodeMaps(Nodes, PredCounts, SuccCounts, BackEdgeInCounts,
                    BackEdgeOutCounts, DominatorChildCounts, DominatedCounts);
    collectEdges(Problem, Nodes, Profile, Summary, PredCounts, SuccCounts,
                 BackEdgeInCounts, BackEdgeOutCounts, SelfLoopNodes,
                 LoopTouchedNodes);
    collectDominatorFacts(Problem, Nodes, DominatorChildCounts,
                          DominatedCounts);
    collectNodeFacts(Problem, Nodes, Profile, Summary, PredCounts, SuccCounts,
                     BackEdgeInCounts, BackEdgeOutCounts, DominatorChildCounts,
                     DominatedCounts, SelfLoopNodes, LoopTouchedNodes);

    Profile.setSummary(Summary);
    return Profile;
  }

private:
  template <typename... MapsT>
  static void reserveNodeMaps(const std::vector<node_t> &Nodes, MapsT &...Maps) {
    (Maps.reserve(Nodes.size()), ...);
    for (const auto &Node : Nodes) {
      (Maps.emplace(Node, 0), ...);
    }
  }

  static void collectEdges(
      const ProblemT &Problem, const std::vector<node_t> &Nodes,
      profile_t &Profile, StaticImportanceSummary &Summary,
      std::unordered_map<node_t, std::size_t> &PredCounts,
      std::unordered_map<node_t, std::size_t> &SuccCounts,
      std::unordered_map<node_t, std::size_t> &BackEdgeInCounts,
      std::unordered_map<node_t, std::size_t> &BackEdgeOutCounts,
      std::unordered_set<node_t> &SelfLoopNodes,
      std::unordered_set<node_t> &LoopTouchedNodes) {
    for (const auto &Src : Nodes) {
      const auto Succs = Problem.succs(Src);
      SuccCounts[Src] = Succs.size();
      for (const auto &Dst : Succs) {
        ++PredCounts[Dst];

        typename profile_t::edge_importance_t Edge;
        Edge.Src = Src;
        Edge.Dst = Dst;
        Edge.IsBackEdge =
            detail::isBackEdgeIfAvailable<ProblemT, node_t>(Problem, Src, Dst);
        Profile.addEdgeImportance(Edge);

        ++Summary.Edges;
        if (Src == Dst) {
          SelfLoopNodes.insert(Src);
        }
        if (Edge.IsBackEdge) {
          ++Summary.BackEdges;
          ++BackEdgeOutCounts[Src];
          ++BackEdgeInCounts[Dst];
          LoopTouchedNodes.insert(Src);
          LoopTouchedNodes.insert(Dst);
        }
      }
    }
  }

  static void collectDominatorFacts(
      const ProblemT &Problem, const std::vector<node_t> &Nodes,
      std::unordered_map<node_t, std::size_t> &DominatorChildCounts,
      std::unordered_map<node_t, std::size_t> &DominatedCounts) {
    if constexpr (detail::HasIdom<ProblemT, node_t>::value) {
      for (const auto &Node : Nodes) {
        if (Node == Problem.entry()) {
          continue;
        }
        ++DominatorChildCounts[Problem.idom(Node)];
      }
    }

    if constexpr (detail::HasDominates<ProblemT, node_t>::value) {
      for (const auto &A : Nodes) {
        for (const auto &B : Nodes) {
          if (A != B && Problem.dominates(A, B)) {
            ++DominatedCounts[A];
          }
        }
      }
    }
  }

  static void collectNodeFacts(
      const ProblemT &Problem, const std::vector<node_t> &Nodes,
      profile_t &Profile, StaticImportanceSummary &Summary,
      const std::unordered_map<node_t, std::size_t> &PredCounts,
      const std::unordered_map<node_t, std::size_t> &SuccCounts,
      const std::unordered_map<node_t, std::size_t> &BackEdgeInCounts,
      const std::unordered_map<node_t, std::size_t> &BackEdgeOutCounts,
      const std::unordered_map<node_t, std::size_t> &DominatorChildCounts,
      const std::unordered_map<node_t, std::size_t> &DominatedCounts,
      const std::unordered_set<node_t> &SelfLoopNodes,
      const std::unordered_set<node_t> &LoopTouchedNodes) {
    for (const auto &Node : Nodes) {
      typename profile_t::node_importance_t Importance;
      Importance.InDegree = PredCounts.at(Node);
      Importance.OutDegree = SuccCounts.at(Node);
      Importance.InOutProduct = Importance.InDegree * Importance.OutDegree;

      Importance.IsEntry = Node == Problem.entry();
      Importance.IsExit = Importance.OutDegree == 0;
      Importance.IsBranch = Importance.OutDegree > 1;
      Importance.IsJoin = Importance.InDegree > 1;
      Importance.IsLinear =
          Importance.InDegree == 1 && Importance.OutDegree == 1;
      Importance.HasSelfLoop = SelfLoopNodes.count(Node) != 0;
      Importance.InLoop = LoopTouchedNodes.count(Node) != 0;
      Importance.BackEdgeInCount = BackEdgeInCounts.at(Node);
      Importance.BackEdgeOutCount = BackEdgeOutCounts.at(Node);
      Importance.IsLoopHeader = Importance.BackEdgeInCount != 0;
      Importance.IsLoopLatch = Importance.BackEdgeOutCount != 0;
      Importance.IsCompressibleLinear =
          Importance.IsLinear && !Importance.IsEntry && !Importance.HasSelfLoop;
      Importance.HasDominatorInfo =
          detail::HasIdom<ProblemT, node_t>::value ||
          detail::HasDominates<ProblemT, node_t>::value;
      Importance.DominatorChildren = DominatorChildCounts.at(Node);
      Importance.DominatedCount = DominatedCounts.at(Node);
      Importance.IsBoundary =
          Importance.IsEntry || Importance.IsExit || Importance.IsBranch ||
          Importance.IsJoin || Importance.IsLoopHeader ||
          Importance.IsLoopLatch || Importance.DominatorChildren > 1;
      Importance.BoundaryScore =
          (Importance.IsEntry ? 1 : 0) + (Importance.IsExit ? 1 : 0) +
          (Importance.IsBranch ? 1 : 0) + (Importance.IsJoin ? 1 : 0) +
          Importance.BackEdgeInCount + Importance.BackEdgeOutCount +
          Importance.DominatorChildren;

      Profile.setNodeImportance(Node, Importance);
      updateSummary(Summary, Importance);
    }
  }

  static void updateSummary(StaticImportanceSummary &Summary,
                            const StaticNodeImportance<node_t> &Importance) {
    ++Summary.Nodes;
    Summary.EntryNodes += Importance.IsEntry ? 1 : 0;
    Summary.ExitNodes += Importance.IsExit ? 1 : 0;
    Summary.BranchNodes += Importance.IsBranch ? 1 : 0;
    Summary.JoinNodes += Importance.IsJoin ? 1 : 0;
    Summary.LinearNodes += Importance.IsLinear ? 1 : 0;
    Summary.CompressibleLinearNodes +=
        Importance.IsCompressibleLinear ? 1 : 0;
    Summary.SelfLoopNodes += Importance.HasSelfLoop ? 1 : 0;
    Summary.LoopTouchedNodes += Importance.InLoop ? 1 : 0;
    Summary.LoopHeaders += Importance.IsLoopHeader ? 1 : 0;
    Summary.LoopLatches += Importance.IsLoopLatch ? 1 : 0;
    Summary.BoundaryNodes += Importance.IsBoundary ? 1 : 0;

    Summary.MaxInDegree =
        std::max(Summary.MaxInDegree, Importance.InDegree);
    Summary.MaxOutDegree =
        std::max(Summary.MaxOutDegree, Importance.OutDegree);
    Summary.MaxInOutProduct =
        std::max(Summary.MaxInOutProduct, Importance.InOutProduct);
    Summary.MaxDominatorChildren =
        std::max(Summary.MaxDominatorChildren, Importance.DominatorChildren);
    Summary.MaxDominatedCount =
        std::max(Summary.MaxDominatedCount, Importance.DominatedCount);
  }
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCEBUILDER_H_
