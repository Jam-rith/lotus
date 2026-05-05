#ifndef DATAFLOW_ELIMINATION_CORE_OPTIONS_H_
#define DATAFLOW_ELIMINATION_CORE_OPTIONS_H_

#include <cstddef>
#include <string>

namespace elimination {

enum class EliminationMethod {
  // Generic O(n^3) state-elimination (Floyd–Warshall-style) over all nodes.
  StateElimination,
  // Paper-style ADT "simple" algorithm path-expression updates.
  ADTSimple,
  // Paper-style ADT + path-expression construction (requires reducible info).
  ADTDelayed,
};

enum class EliminationOrderHeuristic {
  // Preserve the original state-elimination order: reverse reducible
  // topological order when available, otherwise node iteration order.
  Original,
  // Eliminate low fan-in/fan-out nodes first and delay interface-like nodes.
  MinPredSucc,
  // Estimate incremental expression growth from M[i,k], M[k,k], and M[k,j].
  ExpressionAware,
  // Expression-aware base score plus bounded one-step look-ahead over the top
  // heap candidates.
  ExpressionLookahead,
  // Further penalize nodes whose self-loop closure is likely to be expensive.
  StarRisk,
  // Predict elimination cost from an offline-trained order model.
  LearnedCost,
};

enum class OnNonConvergentStar {
  // Abort solve with NonConvergentStar status.
  Fail,
  // Return the last iterand at the iteration bound.
  ReturnLast,
  // Return meet identity at the iteration bound.
  ReturnIdentity,
};

enum class SolveStatus {
  Ok,
  FallbackToState,
  NonConvergentStar,
  InvalidProblem,
};

enum class FallbackReason {
  None,
  ADTRejected,
  InvalidProblem,
};

struct SolveDiagnostics final {
  bool used_adt = false;
  EliminationMethod requested_method = EliminationMethod::StateElimination;
  EliminationMethod executed_method = EliminationMethod::StateElimination;
  EliminationOrderHeuristic requested_order =
      EliminationOrderHeuristic::Original;
  EliminationOrderHeuristic executed_order =
      EliminationOrderHeuristic::Original;
  FallbackReason fallback_reason = FallbackReason::None;
  std::size_t path_construction_us = 0;
  std::size_t final_eval_us = 0;
  std::size_t star_iterations_total = 0;
  bool max_star_hit = false;
};

struct EliminationOptions final {
  EliminationMethod Method = EliminationMethod::StateElimination;
  EliminationOrderHeuristic OrderHeuristic =
      EliminationOrderHeuristic::Original;
  OnNonConvergentStar NonConvergentStarPolicy = OnNonConvergentStar::Fail;
  // 0 means "use Problem.maxStarIterations()".
  std::size_t MaxStarIterations = 0;
  // When true, build/store path expressions but skip final fact evaluation.
  bool SkipFinalEval = false;
  // Reserved for future conditional collection. Diagnostics are currently
  // recorded unconditionally by the solver and attached to result metadata.
  bool RecordDiagnostics = true;
  // Optional training-data trace for state-elimination ordering. When set, the
  // solver appends candidate features and chosen-node proxy labels as TSV rows.
  bool RecordOrderTrace = false;
  std::string OrderTracePath;
  // Frontends can use this to attach a function/benchmark identifier to rows.
  std::string OrderTraceTag;
  // Optional human-readable solver progress trace. 0 disables it. When enabled,
  // state elimination reports every N eliminated nodes on stderr. This is meant
  // for diagnosing large functions that do not finish often enough to emit a
  // per-function run-summary row.
  std::size_t OrderProgressInterval = 0;
  // 0 disables time-based progress. When set, state elimination also reports
  // the current function's eliminated/total vertex count at this interval.
  double OrderProgressTimeIntervalSec = 0.0;
  std::string OrderProgressTag;
  // Optional linear JSON model produced by scripts/apa_train_order_model.py.
  // It is used only when OrderHeuristic is LearnedCost.
  std::string OrderModelPath;
};

} // namespace elimination

#endif // DATAFLOW_ELIMINATION_CORE_OPTIONS_H_
