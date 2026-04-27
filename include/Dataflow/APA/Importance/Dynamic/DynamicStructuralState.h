#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_DYNAMICSTRUCTURALSTATE_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_DYNAMICSTRUCTURALSTATE_H_

#include <algorithm>
#include <cstddef>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>
#include <vector>

namespace elimination {

// Snapshot of one candidate node in the current alive graph. These values
// change as state elimination removes nodes and introduces new summaries.
struct DynamicStructuralNodeStats final {
  std::size_t AlivePredCount = 0;
  std::size_t AliveSuccCount = 0;
  std::size_t AlivePredSuccProduct = 0;

  // Distance-decayed contribution from nearby alive nodes. Direct neighbors
  // keep full weight; distance d contributes pred_succ_product /
  // DecayFactor^(d - 1).
  double DecayedNeighborhoodPredSucc = 0.0;
};

struct DynamicStructuralConfig final {
  std::size_t NeighborhoodDepth = 0;
  double DecayFactor = 2.0;
};

// Maintains adjacency used for dynamic structural importance. This file is
// about alive-node structural data only; expression-matrix growth belongs in
// ExpressionExplosion/EliminationMatrixStats.h.
class DynamicStructuralState final {
public:
  using adjacency_t = std::vector<std::vector<std::size_t>>;

  DynamicStructuralState() = default;

  DynamicStructuralState(adjacency_t Preds, adjacency_t Succs,
                         DynamicStructuralConfig Config = {})
      : Preds(std::move(Preds)), Succs(std::move(Succs)),
        Config(std::move(Config)) {
    resetCaches();
  }

  static DynamicStructuralState fromSuccessors(
      const adjacency_t &Succs, DynamicStructuralConfig Config = {}) {
    adjacency_t Preds(Succs.size());
    for (std::size_t Src = 0; Src < Succs.size(); ++Src) {
      for (const auto Dst : Succs[Src]) {
        if (Dst < Succs.size()) {
          Preds[Dst].push_back(Src);
        }
      }
    }
    return DynamicStructuralState(std::move(Preds), Succs, std::move(Config));
  }

  void initialize(const std::vector<bool> &Alive) {
    CachedAlive = Alive;
    CachedStats.assign(Succs.size(), DynamicStructuralNodeStats());
    NeighborhoodDirty.assign(Succs.size(), true);
    for (std::size_t Node = 0; Node < Succs.size(); ++Node) {
      if (Node < CachedAlive.size() && CachedAlive[Node]) {
        recomputeLocalCounts(Node);
      }
    }
  }

  std::vector<std::size_t> removeNode(std::size_t Node) {
    ensureInitialized();
    std::vector<std::size_t> Dirty;
    if (Node >= CachedAlive.size() || Node >= CachedStats.size() ||
        !CachedAlive[Node]) {
      return Dirty;
    }

    CachedAlive[Node] = false;
    CachedStats[Node] = DynamicStructuralNodeStats();
    markNeighborhoodDirty(Node, Dirty);

    for (const auto Pred : Preds[Node]) {
      if (isAlive(Pred)) {
        if (CachedStats[Pred].AliveSuccCount > 0) {
          --CachedStats[Pred].AliveSuccCount;
        }
        refreshProduct(Pred);
        markNeighborhoodDirty(Pred, Dirty);
      }
    }
    for (const auto Succ : Succs[Node]) {
      if (isAlive(Succ)) {
        if (CachedStats[Succ].AlivePredCount > 0) {
          --CachedStats[Succ].AlivePredCount;
        }
        refreshProduct(Succ);
        markNeighborhoodDirty(Succ, Dirty);
      }
    }

    return Dirty;
  }

  DynamicStructuralNodeStats snapshotNode(std::size_t Node) {
    ensureInitialized();
    if (Node >= CachedStats.size() || !isAlive(Node)) {
      return DynamicStructuralNodeStats();
    }
    refreshNeighborhoodIfDirty(Node);
    return CachedStats[Node];
  }

  DynamicStructuralNodeStats snapshotNode(
      std::size_t Node, const std::vector<bool> &Alive) {
    if (Alive != CachedAlive) {
      initialize(Alive);
    }
    return snapshotNode(Node);
  }

  std::vector<DynamicStructuralNodeStats> snapshotAll() {
    ensureInitialized();
    for (std::size_t Node = 0; Node < CachedStats.size(); ++Node) {
      if (isAlive(Node)) {
        refreshNeighborhoodIfDirty(Node);
      }
    }
    return CachedStats;
  }

  std::vector<DynamicStructuralNodeStats>
  snapshotAll(const std::vector<bool> &Alive) {
    if (Alive != CachedAlive) {
      initialize(Alive);
    }
    return snapshotAll();
  }

  std::vector<std::size_t> dirtyNodesAfterRemoval(std::size_t Node) const {
    std::vector<std::size_t> Dirty;
    if (Node >= Succs.size()) {
      return Dirty;
    }
    collectNeighborhood(Node, Config.NeighborhoodDepth, Dirty);
    return Dirty;
  }

  DynamicStructuralNodeStats computeUncachedSnapshot(
      std::size_t Node, const std::vector<bool> &Alive) const {
    DynamicStructuralNodeStats Snapshot;
    if (Node >= Succs.size() || Node >= Preds.size() || Node >= Alive.size() ||
        !Alive[Node]) {
      return Snapshot;
    }

    Snapshot.AlivePredCount = countAlive(Preds[Node], Alive, Node);
    Snapshot.AliveSuccCount = countAlive(Succs[Node], Alive, Node);
    Snapshot.AlivePredSuccProduct =
        safeMul(Snapshot.AlivePredCount, Snapshot.AliveSuccCount);
    Snapshot.DecayedNeighborhoodPredSucc =
        computeDecayedNeighborhoodPredSucc(Node, Alive);
    return Snapshot;
  }

  std::vector<DynamicStructuralNodeStats> computeUncachedSnapshotAll(
      const std::vector<bool> &Alive) const {
    std::vector<DynamicStructuralNodeStats> Snapshots(Succs.size());
    for (std::size_t Node = 0; Node < Succs.size(); ++Node) {
      Snapshots[Node] = computeUncachedSnapshot(Node, Alive);
    }
    return Snapshots;
  }

  const adjacency_t &preds() const { return Preds; }
  const adjacency_t &succs() const { return Succs; }
  const DynamicStructuralConfig &config() const { return Config; }
  const std::vector<bool> &alive() const { return CachedAlive; }

private:
  void resetCaches() {
    CachedAlive.assign(Succs.size(), true);
    CachedStats.assign(Succs.size(), DynamicStructuralNodeStats());
    NeighborhoodDirty.assign(Succs.size(), true);
  }

  void ensureInitialized() {
    if (CachedStats.size() != Succs.size() ||
        NeighborhoodDirty.size() != Succs.size()) {
      resetCaches();
      initialize(CachedAlive);
    }
  }

  bool isAlive(std::size_t Node) const {
    return Node < CachedAlive.size() && CachedAlive[Node];
  }

  static std::size_t safeMul(std::size_t Lhs, std::size_t Rhs) {
    const auto Max = std::numeric_limits<std::size_t>::max();
    if (Lhs != 0 && Rhs > Max / Lhs) {
      return Max;
    }
    return Lhs * Rhs;
  }

  static std::size_t countAlive(const std::vector<std::size_t> &Neighbors,
                                const std::vector<bool> &Alive,
                                std::size_t Self) {
    std::size_t Count = 0;
    for (const auto Neighbor : Neighbors) {
      if (Neighbor < Alive.size() && Neighbor != Self && Alive[Neighbor]) {
        ++Count;
      }
    }
    return Count;
  }

  void recomputeLocalCounts(std::size_t Node) {
    if (Node >= CachedStats.size() || !isAlive(Node)) {
      return;
    }
    CachedStats[Node].AlivePredCount =
        countAlive(Preds[Node], CachedAlive, Node);
    CachedStats[Node].AliveSuccCount =
        countAlive(Succs[Node], CachedAlive, Node);
    refreshProduct(Node);
  }

  void refreshProduct(std::size_t Node) {
    CachedStats[Node].AlivePredSuccProduct =
        safeMul(CachedStats[Node].AlivePredCount,
                CachedStats[Node].AliveSuccCount);
  }

  void refreshNeighborhoodIfDirty(std::size_t Node) {
    if (Node >= NeighborhoodDirty.size() || !NeighborhoodDirty[Node]) {
      return;
    }
    CachedStats[Node].DecayedNeighborhoodPredSucc =
        computeDecayedNeighborhoodPredSucc(Node, CachedAlive);
    NeighborhoodDirty[Node] = false;
  }

  void markNeighborhoodDirty(std::size_t Node,
                             std::vector<std::size_t> &Dirty) {
    std::vector<std::size_t> LocalDirty;
    collectNeighborhood(Node, Config.NeighborhoodDepth, LocalDirty);
    for (const auto Candidate : LocalDirty) {
      if (Candidate < NeighborhoodDirty.size()) {
        NeighborhoodDirty[Candidate] = true;
      }
      if (std::find(Dirty.begin(), Dirty.end(), Candidate) == Dirty.end()) {
        Dirty.push_back(Candidate);
      }
    }
  }

  void collectNeighborhood(std::size_t Root, std::size_t Depth,
                           std::vector<std::size_t> &Out) const {
    if (Root >= Succs.size()) {
      return;
    }
    std::unordered_set<std::size_t> Seen;
    std::queue<std::pair<std::size_t, std::size_t>> Worklist;
    Seen.insert(Root);
    Worklist.push({Root, 0});

    while (!Worklist.empty()) {
      const auto Current = Worklist.front();
      Worklist.pop();
      Out.push_back(Current.first);
      if (Current.second >= Depth) {
        continue;
      }
      auto Visit = [&](std::size_t Next) {
        if (Next < Succs.size() && Seen.insert(Next).second) {
          Worklist.push({Next, Current.second + 1});
        }
      };
      for (const auto Pred : Preds[Current.first]) {
        Visit(Pred);
      }
      for (const auto Succ : Succs[Current.first]) {
        Visit(Succ);
      }
    }
  }

  double computeDecayedNeighborhoodPredSucc(
      std::size_t Root, const std::vector<bool> &Alive) const {
    if (Config.NeighborhoodDepth == 0 || Config.DecayFactor <= 0.0) {
      return 0.0;
    }

    std::vector<std::size_t> Distance(Succs.size(),
                                      std::numeric_limits<std::size_t>::max());
    std::queue<std::size_t> Worklist;
    Distance[Root] = 0;
    Worklist.push(Root);

    double Score = 0.0;
    while (!Worklist.empty()) {
      const auto Current = Worklist.front();
      Worklist.pop();
      const auto NextDistance = Distance[Current] + 1;
      if (NextDistance > Config.NeighborhoodDepth) {
        continue;
      }

      addNeighbors(Current, NextDistance, Alive, Distance, Worklist, Score);
    }
    return Score;
  }

  void addNeighbors(std::size_t Current, std::size_t NextDistance,
                    const std::vector<bool> &Alive,
                    std::vector<std::size_t> &Distance,
                    std::queue<std::size_t> &Worklist, double &Score) const {
    auto Visit = [&](std::size_t Neighbor) {
      if (Neighbor >= Alive.size() || Neighbor >= Distance.size() ||
          !Alive[Neighbor] || Distance[Neighbor] <= NextDistance) {
        return;
      }
      Distance[Neighbor] = NextDistance;
      Worklist.push(Neighbor);

      const auto PredCount = countAlive(Preds[Neighbor], Alive, Neighbor);
      const auto SuccCount = countAlive(Succs[Neighbor], Alive, Neighbor);
      double Weight = 1.0;
      for (std::size_t I = 1; I < NextDistance; ++I) {
        Weight /= Config.DecayFactor;
      }
      Score += static_cast<double>(safeMul(PredCount, SuccCount)) * Weight;
    };

    for (const auto Pred : Preds[Current]) {
      Visit(Pred);
    }
    for (const auto Succ : Succs[Current]) {
      Visit(Succ);
    }
  }

  adjacency_t Preds;
  adjacency_t Succs;
  DynamicStructuralConfig Config;
  std::vector<bool> CachedAlive;
  std::vector<DynamicStructuralNodeStats> CachedStats;
  std::vector<bool> NeighborhoodDirty;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_DYNAMICSTRUCTURALSTATE_H_
