#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_PATHEXPRESSIONSTATS_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_PATHEXPRESSIONSTATS_H_

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

namespace elimination {

// Symbol-only statistics for a path-expression DAG. This layer intentionally
// does not evaluate or propagate facts; it only describes expression shape.
struct PathExpressionStats final {
  std::size_t UniqueNodeCount = 0;
  std::size_t SharedRefCount = 0;
  std::size_t MaxDepth = 0;

  std::size_t ZeroCount = 0;
  std::size_t OneCount = 0;
  std::size_t AtomCount = 0;
  std::size_t UnionCount = 0;
  std::size_t ConcatCount = 0;
  std::size_t StarCount = 0;

  // Reserved for future expression kinds. The current PathExprFactory encodes
  // regular expressions with Union/Concat/Star and has no explicit Plus node.
  std::size_t PlusCount = 0;
};

template <typename ExprFactoryT> class PathExpressionStatsCollector final {
public:
  using expr_ref_t = typename ExprFactoryT::Ref;

  PathExpressionStats collect(const expr_ref_t &Expr) {
    Stats = PathExpressionStats();
    Seen.clear();
    DepthMemo.clear();
    Stats.MaxDepth = collectImpl(Expr);
    Stats.UniqueNodeCount = Seen.size();
    return Stats;
  }

private:
  std::size_t collectImpl(const expr_ref_t &Expr) {
    if (!Expr) {
      return 0;
    }

    const auto *Key = static_cast<const void *>(Expr.get());
    auto MemoIt = DepthMemo.find(Key);
    if (MemoIt != DepthMemo.end()) {
      ++Stats.SharedRefCount;
      return MemoIt->second;
    }

    Seen.insert(Key);
    std::size_t Depth = 1;
    switch (Expr->K) {
    case ExprFactoryT::Kind::Zero:
      ++Stats.ZeroCount;
      break;
    case ExprFactoryT::Kind::One:
      ++Stats.OneCount;
      break;
    case ExprFactoryT::Kind::Atom:
      ++Stats.AtomCount;
      break;
    case ExprFactoryT::Kind::Union:
      ++Stats.UnionCount;
      Depth = 1 + std::max(collectImpl(Expr->L), collectImpl(Expr->R));
      break;
    case ExprFactoryT::Kind::Concat:
      ++Stats.ConcatCount;
      Depth = 1 + std::max(collectImpl(Expr->L), collectImpl(Expr->R));
      break;
    case ExprFactoryT::Kind::Star:
      ++Stats.StarCount;
      Depth = 1 + collectImpl(Expr->L);
      break;
    }

    DepthMemo.emplace(Key, Depth);
    return Depth;
  }

  PathExpressionStats Stats;
  std::unordered_set<const void *> Seen;
  std::unordered_map<const void *, std::size_t> DepthMemo;
};

template <typename ExprFactoryT>
PathExpressionStats
collectPathExpressionStats(typename ExprFactoryT::Ref Expr) {
  PathExpressionStatsCollector<ExprFactoryT> Collector;
  return Collector.collect(Expr);
}

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_STATEELIMINATION_PATHEXPRESSIONSTATS_H_
