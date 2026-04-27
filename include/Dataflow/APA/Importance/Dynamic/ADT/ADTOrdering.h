#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTORDERING_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ADT_ADTORDERING_H_

#include "Dataflow/APA/Importance/Dynamic/IncrementalOrderingHeap.h"
#include "Dataflow/APA/Importance/Dynamic/LinearOrderingModel.h"

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
