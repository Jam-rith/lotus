#ifndef DATAFLOW_APA_IMPORTANCE_SPARSE_SPARSEBUILDER_H_
#define DATAFLOW_APA_IMPORTANCE_SPARSE_SPARSEBUILDER_H_

#include "Dataflow/APA/Importance/Sparse/SparseProfile.h"
#include "Dataflow/APA/Importance/Static/StaticImportanceProfile.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace elimination {

// Builds a compact sparse profile from immutable static importance facts and
// the original CFG. This records compression potential only; it does not
// rewrite edges or compose transfer functions.
template <typename ProblemT> class SparseBuilder final {
public:
  using node_t = typename ProblemT::n_t;
  using sparse_profile_t = SparseProfile<node_t>;
  using static_profile_t = StaticImportanceProfile<node_t>;

  sparse_profile_t build(const ProblemT &Problem,
                         const static_profile_t &StaticProfile) const {
    sparse_profile_t Profile;
    SparseSummary Summary;

    const auto Nodes = Problem.nodes();
    Summary.TotalNodes = static_cast<std::uint32_t>(Nodes.size());

    for (const auto &Node : Nodes) {
      auto Info = buildNodeInfo(Node, StaticProfile);
      updateSummaryForNode(Info, Summary);
      Profile.setNodeInfo(Node, Info);
    }

    assignLinearRegions(Problem, Nodes, Profile, Summary);
    Summary.CompressionRatio =
        Summary.TotalNodes == 0
            ? 0.0
            : static_cast<double>(Summary.EstimatedRemovedNodes) /
                  static_cast<double>(Summary.TotalNodes);
    Profile.setSummary(Summary);
    return Profile;
  }

private:
  static SparseNodeInfo<node_t>
  buildNodeInfo(const node_t &Node, const static_profile_t &StaticProfile) {
    SparseNodeInfo<node_t> Info;
    const auto *Static = StaticProfile.findNodeImportance(Node);
    if (Static == nullptr) {
      Info.Flags |= flagValue(SparseNodeFlag::MustPreserve);
      return Info;
    }

    if (Static->IsLinear) {
      Info.Flags |= flagValue(SparseNodeFlag::Linear);
    }
    addPreserveReasons(*Static, Info);
    if (Info.PreserveReasons != 0) {
      Info.Flags |= flagValue(SparseNodeFlag::MustPreserve);
    }

    const bool Compressible =
        Static->IsLinear && !Static->IsEntry && !Static->IsExit &&
        !Static->IsLoopHeader && !Static->IsLoopLatch &&
        !Static->HasSelfLoop && Info.PreserveReasons == 0;
    if (Compressible) {
      Info.Flags |= flagValue(SparseNodeFlag::Candidate);
      Info.Flags |= flagValue(SparseNodeFlag::Compressible);
    }
    return Info;
  }

  static void addPreserveReasons(const StaticNodeImportance<node_t> &Static,
                                 SparseNodeInfo<node_t> &Info) {
    if (Static.IsEntry) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::Entry);
    }
    if (Static.IsExit) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::Exit);
    }
    if (Static.IsBranch) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::Branch);
    }
    if (Static.IsJoin) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::Join);
    }
    if (Static.IsLoopHeader) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::LoopHeader);
    }
    if (Static.IsLoopLatch) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::LoopLatch);
    }
    if (Static.HasSelfLoop) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::SelfLoop);
    }
    if (Static.IsBoundary) {
      Info.PreserveReasons |= reasonValue(SparsePreserveReason::Boundary);
    }
  }

  static void updateSummaryForNode(const SparseNodeInfo<node_t> &Info,
                                   SparseSummary &Summary) {
    Summary.CandidateNodes +=
        Info.hasFlag(SparseNodeFlag::Candidate) ? 1u : 0u;
    Summary.CompressibleNodes +=
        Info.hasFlag(SparseNodeFlag::Compressible) ? 1u : 0u;
    Summary.MustPreserveNodes +=
        Info.hasFlag(SparseNodeFlag::MustPreserve) ? 1u : 0u;
  }

  static void assignLinearRegions(const ProblemT &Problem,
                                  const std::vector<node_t> &Nodes,
                                  sparse_profile_t &Profile,
                                  SparseSummary &Summary) {
    std::unordered_set<node_t> Visited;
    std::uint32_t NextRegionId = 0;

    for (const auto &Node : Nodes) {
      auto *Info = Profile.findNodeInfo(Node);
      if (Info == nullptr || !Info->hasFlag(SparseNodeFlag::Compressible) ||
          Visited.count(Node) != 0) {
        continue;
      }

      typename sparse_profile_t::region_info_t Region;
      Region.RegionId = NextRegionId++;
      Region.EntryNode = Node;
      Region.ExitNode = Node;
      Region.Flags |= flagValue(SparseRegionFlag::LinearChain);
      Region.Flags |= flagValue(SparseRegionFlag::Compressible);

      auto Current = Node;
      while (true) {
        auto *CurrentInfo = Profile.findNodeInfo(Current);
        if (CurrentInfo == nullptr ||
            !CurrentInfo->hasFlag(SparseNodeFlag::Compressible) ||
            Visited.count(Current) != 0) {
          break;
        }

        Visited.insert(Current);
        CurrentInfo->Flags |= flagValue(SparseNodeFlag::InRegion);
        CurrentInfo->RegionId = Region.RegionId;
        Region.ExitNode = Current;
        ++Region.NodeCount;
        ++Region.CompressibleNodeCount;

        const auto Succs = Problem.succs(Current);
        if (Succs.size() != 1) {
          break;
        }
        const auto Next = Succs.front();
        const auto *NextInfo = Profile.findNodeInfo(Next);
        if (NextInfo == nullptr ||
            !NextInfo->hasFlag(SparseNodeFlag::Compressible)) {
          break;
        }
        Current = Next;
      }

      Region.EstimatedRemovedNodes =
          Region.NodeCount == 0 ? 0 : Region.NodeCount - 1;
      Summary.EstimatedRemovedNodes += Region.EstimatedRemovedNodes;
      ++Summary.Regions;
      ++Summary.CompressibleRegions;
      Profile.addRegion(Region);
    }
  }
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_SPARSE_SPARSEBUILDER_H_
