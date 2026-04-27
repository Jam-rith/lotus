#ifndef DATAFLOW_APA_IMPORTANCE_DYNAMIC_ORDERING_INCREMENTALORDERINGHEAP_H_
#define DATAFLOW_APA_IMPORTANCE_DYNAMIC_ORDERING_INCREMENTALORDERINGHEAP_H_

#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace elimination {

// Versioned min-heap for learned or heuristic candidate ordering.
//
// The heap never scans all candidates after initialization. Solvers notify it
// about candidates whose features may have changed; stale heap entries are
// discarded lazily using per-candidate versions.
class IncrementalOrderingHeap final {
public:
  using score_callback_t = std::function<double(std::size_t)>;
  using alive_callback_t = std::function<bool(std::size_t)>;

  IncrementalOrderingHeap() = default;

  IncrementalOrderingHeap(std::size_t CandidateCount,
                          score_callback_t ScoreFn,
                          alive_callback_t AliveFn)
      : Versions(CandidateCount, 0), ScoreCallback(std::move(ScoreFn)),
        AliveCallback(std::move(AliveFn)) {}

  void reset(std::size_t CandidateCount, score_callback_t ScoreFn,
             alive_callback_t AliveFn) {
    Versions.assign(CandidateCount, 0);
    Heap = heap_t();
    ScoreCallback = std::move(ScoreFn);
    AliveCallback = std::move(AliveFn);
  }

  void push(std::size_t Candidate) {
    if (!canUse(Candidate)) {
      return;
    }
    Heap.push({ScoreCallback(Candidate), Candidate, Versions[Candidate]});
  }

  void initializeAll() {
    for (std::size_t Candidate = 0; Candidate < Versions.size(); ++Candidate) {
      push(Candidate);
    }
  }

  void refresh(std::size_t Candidate) {
    if (Candidate >= Versions.size()) {
      return;
    }
    ++Versions[Candidate];
    push(Candidate);
  }

  template <typename RangeT> void refreshAll(const RangeT &Candidates) {
    for (const auto Candidate : Candidates) {
      refresh(static_cast<std::size_t>(Candidate));
    }
  }

  void invalidate(std::size_t Candidate) {
    if (Candidate < Versions.size()) {
      ++Versions[Candidate];
    }
  }

  std::size_t popMin() {
    while (!Heap.empty()) {
      const auto Entry = Heap.top();
      Heap.pop();
      if (Entry.Candidate >= Versions.size()) {
        continue;
      }
      if (Entry.Version != Versions[Entry.Candidate]) {
        continue;
      }
      if (!canUse(Entry.Candidate)) {
        continue;
      }
      return Entry.Candidate;
    }
    return std::numeric_limits<std::size_t>::max();
  }

private:
  struct Entry final {
    double Score = 0.0;
    std::size_t Candidate = 0;
    std::size_t Version = 0;
  };

  struct Greater final {
    bool operator()(const Entry &Lhs, const Entry &Rhs) const {
      if (Lhs.Score != Rhs.Score) {
        return Lhs.Score > Rhs.Score;
      }
      return Lhs.Candidate > Rhs.Candidate;
    }
  };

  using heap_t = std::priority_queue<Entry, std::vector<Entry>, Greater>;

  bool canUse(std::size_t Candidate) const {
    return Candidate < Versions.size() && AliveCallback &&
           AliveCallback(Candidate);
  }

  std::vector<std::size_t> Versions;
  heap_t Heap;
  score_callback_t ScoreCallback;
  alive_callback_t AliveCallback;
};

} // namespace elimination

#endif // DATAFLOW_APA_IMPORTANCE_DYNAMIC_ORDERING_INCREMENTALORDERINGHEAP_H_
