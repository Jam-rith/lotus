#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTORDERING_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTORDERING_H_

#include "Dataflow/APA/Importance/Dynamic/IncrementalOrderingHeap.h"
#include "Dataflow/APA/Importance/Dynamic/LinearOrderingModel.h"

#include "Dataflow/APA/Core/Options.h"

#include <cstddef>
#include <vector>

namespace elimination {

// Shared ADT ordering adapter.
//
// ADTSimple and ADTDelayed do not eliminate arbitrary CFG matrix nodes; their
// candidates are ADT composition nodes. This adapter intentionally exposes
// those candidates through the same learned-ordering feature vocabulary used by
// state elimination. The current ADT engines still execute the paper's
// structural postorder, but future topology/F-B/star-aware ADT ordering should
// plug into this adapter instead of creating a second model path.
template <typename ADTNodeT> class ADTOrderingFeatureProvider final {
public:
  using node_t = ADTNodeT;

  OrderingFeatureVector features(const node_t *Node) const {
    OrderingFeatureVector Features;
    if (!Node || Node->Leaf) {
      return Features;
    }

    const auto FCount = static_cast<double>(Node->F.size());
    const auto BCount = static_cast<double>(Node->B.size());
    Features.Score = FCount * BCount;
    Features.AlivePredCount = FCount;
    Features.AliveSuccCount = BCount;
    Features.PredSuccProduct = FCount * BCount;
    Features.CrossCombinationCount = FCount * BCount;
    // In ADT, many downstream costs are driven by X/Y construction at an
    // interval boundary rather than classic matrix fill-in.
    Features.FillInCount = FCount;
    Features.ExistingTargetCount = BCount;
    return Features;
  }
};

template <typename ADTNodeT> std::size_t adtRegionSize(const ADTNodeT *Node) {
  if (!Node || Node->MaxPos < Node->MinPos) {
    return 0;
  }
  return static_cast<std::size_t>(Node->MaxPos - Node->MinPos + 1);
}

template <typename ADTNodeT>
std::size_t adtBoundaryProduct(const ADTNodeT *Node) {
  if (!Node || Node->Leaf) {
    return 0;
  }
  return Node->F.size() * Node->B.size();
}

// Lightweight ADT-local ordering. ADT composition has tree dependencies, so we
// do not globally permute composition nodes. The legal knob is deciding which
// child subtree to evaluate first before evaluating the parent composition.
template <typename ADTNodeT>
std::size_t adtSimpleOrderScore(const ADTNodeT *Node,
                                EliminationOrderHeuristic Heuristic) {
  if (!Node) {
    return 0;
  }
  const auto Region = adtRegionSize(Node);
  const auto Boundary = adtBoundaryProduct(Node);
  switch (Heuristic) {
  case EliminationOrderHeuristic::MinPredSucc:
    return Boundary;
  case EliminationOrderHeuristic::ExpressionAware:
    return Boundary + Region;
  case EliminationOrderHeuristic::StarRisk:
    return Boundary + Region +
           ((Node->F.empty() || Node->B.empty()) ? 0 : Region);
  case EliminationOrderHeuristic::LearnedCost:
  case EliminationOrderHeuristic::Original:
    return Region;
  }
  return Region;
}

template <typename ADTNodeT>
bool shouldVisitLeftADTSubtreeFirst(const ADTNodeT *Parent,
                                    EliminationOrderHeuristic Heuristic) {
  if (!Parent || !Parent->Left || !Parent->Right) {
    return true;
  }
  if (Heuristic == EliminationOrderHeuristic::Original) {
    return true;
  }
  const auto LeftScore = adtSimpleOrderScore(Parent->Left, Heuristic);
  const auto RightScore = adtSimpleOrderScore(Parent->Right, Heuristic);
  if (LeftScore != RightScore) {
    return LeftScore < RightScore;
  }
  return adtRegionSize(Parent->Left) <= adtRegionSize(Parent->Right);
}

template <typename ADTNodeT> class ADTLinearOrderingQueue final {
public:
  using node_t = ADTNodeT;

  bool initialize(const std::vector<node_t *> &Candidates,
                  const LinearOrderingModel &Model) {
    this->Candidates = Candidates;
    this->Model = &Model;
    Alive.assign(Candidates.size(), true);
    Heap.reset(
        Candidates.size(),
        [&](std::size_t Index) {
          return this->Model->predict(
              Provider.features(this->Candidates[Index]));
        },
        [&](std::size_t Index) {
          return Index < Alive.size() && Alive[Index] &&
                 Index < this->Candidates.size() &&
                 this->Candidates[Index] != nullptr;
        });
    Heap.initializeAll();
    return Model.isLoaded();
  }

  node_t *pop() {
    const auto Index = Heap.popMin();
    if (Index >= Candidates.size()) {
      return nullptr;
    }
    Alive[Index] = false;
    Heap.invalidate(Index);
    return Candidates[Index];
  }

  void refresh(std::size_t Index) { Heap.refresh(Index); }

private:
  std::vector<node_t *> Candidates;
  std::vector<bool> Alive;
  const LinearOrderingModel *Model = nullptr;
  ADTOrderingFeatureProvider<node_t> Provider;
  IncrementalOrderingHeap Heap;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTORDERING_H_
