#ifndef DATAFLOW_APA_IMPORTANCE_IMPORTANCE_H_
#define DATAFLOW_APA_IMPORTANCE_IMPORTANCE_H_

// Importance-aware APA is organized into three data layers:
//
//   Static/
//     Immutable CFG, loop, boundary, and dominator facts.
//
//   Dynamic/
//     Facts that change during elimination, such as alive predecessor/successor
//     counts and decayed neighborhood structure.
//
//   ExpressionExplosion/
//     Path-expression DAG and elimination-matrix statistics used to estimate
//     expression growth, fill-in, reuse, and star risk.
//
// ImportanceModel and ImportancePolicy consume these layers but should not
// collect raw facts themselves.
#include "Dataflow/APA/Importance/ExpressionExplosion/ExpressionExplosion.h"
#include "Dataflow/APA/Importance/Dynamic/DynamicImportance.h"
#include "Dataflow/APA/Importance/ImportanceModel.h"
#include "Dataflow/APA/Importance/ImportancePolicy.h"
#include "Dataflow/APA/Importance/Static/StaticImportance.h"

#endif // DATAFLOW_APA_IMPORTANCE_IMPORTANCE_H_
