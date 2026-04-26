#ifndef DATAFLOW_APA_IMPORTANCE_DEMAND_DEMANDBUILDER_H_
#define DATAFLOW_APA_IMPORTANCE_DEMAND_DEMANDBUILDER_H_

#include "Dataflow/APA/Importance/Demand/DemandProfile.h"
#include "Dataflow/APA/Importance/Static/StaticImportanceProfile.h"

#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace elimination {

// Builds a compact demand profile for a set of query targets.
//
// Direction convention for this first version:
//   A node is demand-relevant if it can reach at least one query target in the
//   problem's flow direction. The builder computes this by reverse BFS from the
//   query targets over CFG edges.
template <typename ProblemT> class DemandBuilder final {
public:
  using node_t = typename ProblemT::n_t;
  using demand_profile_t = DemandProfile<node_t>;
  using static_profile_t = StaticImportanceProfile<node_t>;

  demand_profile_t build(const ProblemT &Problem,
                         const static_profile_t &StaticProfile,
                         const std::vector<node_t> &QueryTargets) const {
    demand_profile_t Profile;
    DemandSummary Summary;

    const auto Nodes = Problem.nodes();
    Summary.TotalNodes = static_cast<std::uint32_t>(Nodes.size());
    std::unordered_map<node_t, std::vector<node_t>> Preds;
    std::unordered_set<node_t> QuerySet(QueryTargets.begin(),
                                        QueryTargets.end());

    initializeNodes(Nodes, Profile, Summary);
    buildPredecessors(Problem, Nodes, Preds, Summary);
    markDemandRelevant(Nodes, Preds, QuerySet, Profile);
    finalizeNodeReasons(StaticProfile, Profile, Summary);

    Summary.EstimatedSkippedNodes =
        Summary.TotalNodes >= Summary.DemandRelevantNodes
            ? Summary.TotalNodes - Summary.DemandRelevantNodes
            : 0;
    Summary.DemandReductionRatio =
        Summary.TotalNodes == 0
            ? 0.0
            : static_cast<double>(Summary.EstimatedSkippedNodes) /
                  static_cast<double>(Summary.TotalNodes);
    Profile.setSummary(Summary);
    return Profile;
  }

private:
  static void initializeNodes(const std::vector<node_t> &Nodes,
                              demand_profile_t &Profile,
                              DemandSummary &Summary) {
    for (const auto &Node : Nodes) {
      typename demand_profile_t::node_info_t Info;
      Info.Flags |= flagValue(DemandNodeFlag::CanDelay);
      Profile.setNodeInfo(Node, Info);
    }
    (void)Summary;
  }

  static void buildPredecessors(
      const ProblemT &Problem, const std::vector<node_t> &Nodes,
      std::unordered_map<node_t, std::vector<node_t>> &Preds,
      DemandSummary &Summary) {
    Preds.reserve(Nodes.size());
    for (const auto &Node : Nodes) {
      Preds.emplace(Node, std::vector<node_t>());
    }

    for (const auto &Src : Nodes) {
      const auto Succs = Problem.succs(Src);
      Summary.TotalEdges += static_cast<std::uint32_t>(Succs.size());
      for (const auto &Dst : Succs) {
        Preds[Dst].push_back(Src);
      }
    }
  }

  static void markDemandRelevant(
      const std::vector<node_t> &Nodes,
      const std::unordered_map<node_t, std::vector<node_t>> &Preds,
      const std::unordered_set<node_t> &QuerySet, demand_profile_t &Profile) {
    (void)Nodes;
    std::queue<node_t> Worklist;

    for (const auto &Query : QuerySet) {
      auto *Info = Profile.findNodeInfo(Query);
      if (Info == nullptr) {
        continue;
      }
      Info->Flags |= flagValue(DemandNodeFlag::QueryTarget);
      Info->Flags |= flagValue(DemandNodeFlag::DemandRelevant);
      Info->Flags |= flagValue(DemandNodeFlag::ShouldMaterialize);
      Info->Flags &= ~flagValue(DemandNodeFlag::CanDelay);
      Info->Reasons |= reasonValue(DemandReason::QueryTarget);
      Info->DemandDistance = 0;
      Worklist.push(Query);
    }

    while (!Worklist.empty()) {
      const auto Current = Worklist.front();
      Worklist.pop();
      const auto *CurrentInfo = Profile.findNodeInfo(Current);
      if (CurrentInfo == nullptr) {
        continue;
      }

      auto PredIt = Preds.find(Current);
      if (PredIt == Preds.end()) {
        continue;
      }
      const auto NextDistance = safeDistanceIncrement(
          CurrentInfo->DemandDistance);
      for (const auto &Pred : PredIt->second) {
        auto *PredInfo = Profile.findNodeInfo(Pred);
        if (PredInfo == nullptr || PredInfo->DemandDistance <= NextDistance) {
          continue;
        }
        PredInfo->Flags |= flagValue(DemandNodeFlag::DemandRelevant);
        PredInfo->Flags |= flagValue(DemandNodeFlag::ShouldMaterialize);
        PredInfo->Flags &= ~flagValue(DemandNodeFlag::CanDelay);
        PredInfo->Reasons |= reasonValue(DemandReason::OnDemandPath);
        PredInfo->DemandDistance = NextDistance;
        Worklist.push(Pred);
      }
    }
  }

  static std::uint32_t safeDistanceIncrement(std::uint32_t Distance) {
    const auto Max = std::numeric_limits<std::uint32_t>::max();
    return Distance == Max ? Max : Distance + 1;
  }

  static void finalizeNodeReasons(const static_profile_t &StaticProfile,
                                  demand_profile_t &Profile,
                                  DemandSummary &Summary) {
    std::uint64_t DistanceSum = 0;
    for (auto &Entry : Profile.nodes()) {
      const auto &Node = Entry.first;
      auto &Info = Entry.second;
      const auto *Static = StaticProfile.findNodeImportance(Node);
      if (Static != nullptr) {
        if (Static->IsBoundary) {
          Info.Reasons |= reasonValue(DemandReason::Boundary);
        }
        if (Static->IsEntry) {
          Info.Reasons |= reasonValue(DemandReason::Entry);
        }
        if (Static->IsExit) {
          Info.Reasons |= reasonValue(DemandReason::Exit);
        }
      }

      Summary.QueryTargets +=
          Info.hasFlag(DemandNodeFlag::QueryTarget) ? 1u : 0u;
      Summary.DemandRelevantNodes +=
          Info.hasFlag(DemandNodeFlag::DemandRelevant) ? 1u : 0u;
      Summary.ShouldMaterializeNodes +=
          Info.hasFlag(DemandNodeFlag::ShouldMaterialize) ? 1u : 0u;
      Summary.CanDelayNodes += Info.hasFlag(DemandNodeFlag::CanDelay) ? 1u : 0u;

      if (Info.hasFlag(DemandNodeFlag::DemandRelevant) &&
          Info.DemandDistance != std::numeric_limits<std::uint32_t>::max()) {
        DistanceSum += Info.DemandDistance;
        if (Info.DemandDistance > Summary.MaxDemandDistance) {
          Summary.MaxDemandDistance = Info.DemandDistance;
        }
      }
    }

    Summary.AverageDemandDistance =
        Summary.DemandRelevantNodes == 0
            ? 0.0
            : static_cast<double>(DistanceSum) /
                  static_cast<double>(Summary.DemandRelevantNodes);
    addRegionSummaries(Profile, Summary);
  }

  static void addRegionSummaries(demand_profile_t &Profile,
                                 DemandSummary &Summary) {
    typename demand_profile_t::region_info_t Region;
    Region.RegionId = 0;
    bool HasRegion = false;
    auto &Nodes = Profile.nodes();
    for (auto &Entry : Nodes) {
      const auto &Node = Entry.first;
      auto &Info = Entry.second;
      if (!Info.hasFlag(DemandNodeFlag::DemandRelevant)) {
        continue;
      }
      if (!HasRegion) {
        Region.SeedNode = Node;
        HasRegion = true;
      }
      Info.RegionId = Region.RegionId;
      Info.Flags |= flagValue(DemandNodeFlag::InRegion);
      ++Region.NodeCount;
      Region.QueryTargetCount +=
          Info.hasFlag(DemandNodeFlag::QueryTarget) ? 1u : 0u;
    }

    if (HasRegion) {
      Summary.DemandRegions = 1;
      Region.EstimatedSkippedNodes =
          Summary.TotalNodes >= Region.NodeCount
              ? Summary.TotalNodes - Region.NodeCount
              : 0;
      Profile.addRegion(Region);
    }
  }
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DEMAND_DEMANDBUILDER_H_
