#ifndef DATAFLOW_APA_IMPORTANCE_IMPORTANCEPOLICY_H_
#define DATAFLOW_APA_IMPORTANCE_IMPORTANCEPOLICY_H_

#include "Dataflow/APA/Importance/ImportanceModel.h"

#include <cstddef>
#include <vector>

namespace elimination {

// Adapter layer between the shared importance model and a concrete APA engine.
// Policies are deliberately separated because state elimination and ADT have
// different legality constraints.
template <typename NodeT> class StateEliminationImportancePolicy {
public:
  using node_t = NodeT;
  using profile_t = StaticImportanceProfile<NodeT>;
  using model_t = ImportanceModel<NodeT>;

  StateEliminationImportancePolicy(const profile_t *Profile = nullptr,
                                   const model_t *Model = nullptr)
      : Profile(Profile), Model(Model) {}

  template <typename OrderT> OrderT refineOrder(OrderT Order) const {
    return Order;
  }

  bool enabled() const { return Profile != nullptr && Model != nullptr; }

private:
  const profile_t *Profile = nullptr;
  const model_t *Model = nullptr;
};

template <typename NodeT> class ADTImportancePolicy {
public:
  using node_t = NodeT;
  using profile_t = StaticImportanceProfile<NodeT>;
  using model_t = ImportanceModel<NodeT>;

  ADTImportancePolicy(const profile_t *Profile = nullptr,
                      const model_t *Model = nullptr)
      : Profile(Profile), Model(Model) {}

  template <typename ChildrenT> ChildrenT refineDominatorChildren(
      const NodeT &Parent, ChildrenT Children) const {
    (void)Parent;
    return Children;
  }

  bool enabled() const { return Profile != nullptr && Model != nullptr; }

private:
  const profile_t *Profile = nullptr;
  const model_t *Model = nullptr;
};

template <typename NodeT> class SparseCompressionImportancePolicy {
public:
  using node_t = NodeT;
  using profile_t = StaticImportanceProfile<NodeT>;
  using model_t = ImportanceModel<NodeT>;

  SparseCompressionImportancePolicy(const profile_t *Profile = nullptr,
                                    const model_t *Model = nullptr)
      : Profile(Profile), Model(Model) {}

  bool shouldCompress(const NodeT &Node) const {
    (void)Node;
    return false;
  }

  bool enabled() const { return Profile != nullptr && Model != nullptr; }

private:
  const profile_t *Profile = nullptr;
  const model_t *Model = nullptr;
};

template <typename NodeT> class DemandDrivenImportancePolicy {
public:
  using node_t = NodeT;
  using profile_t = StaticImportanceProfile<NodeT>;
  using model_t = ImportanceModel<NodeT>;

  DemandDrivenImportancePolicy(const profile_t *Profile = nullptr,
                               const model_t *Model = nullptr)
      : Profile(Profile), Model(Model) {}

  bool shouldMaterialize(const NodeT &Node) const {
    (void)Node;
    return true;
  }

  bool enabled() const { return Profile != nullptr && Model != nullptr; }

private:
  const profile_t *Profile = nullptr;
  const model_t *Model = nullptr;
};

template <typename NodeT> class IncrementalUpdateImportancePolicy {
public:
  using node_t = NodeT;
  using profile_t = StaticImportanceProfile<NodeT>;
  using model_t = ImportanceModel<NodeT>;

  IncrementalUpdateImportancePolicy(const profile_t *Profile = nullptr,
                                    const model_t *Model = nullptr)
      : Profile(Profile), Model(Model) {}

  bool shouldReuseCachedSummary(const NodeT &Node) const {
    (void)Node;
    return false;
  }

  bool enabled() const { return Profile != nullptr && Model != nullptr; }

private:
  const profile_t *Profile = nullptr;
  const model_t *Model = nullptr;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_IMPORTANCEPOLICY_H_
