#ifndef DATAFLOW_APA_IMPORTANCE_IMPORTANCE_H_
#define DATAFLOW_APA_IMPORTANCE_IMPORTANCE_H_

// Importance-aware APA is organized around two kinds of facts:
//
//   Static/, Sparse/, Demand/
//     Mostly immutable or pre-solve profiles. They describe the original CFG,
//     sparsification opportunity, and demand/query relevance.
//
//   Dynamic/
//     Runtime statistics collected while a solver is running:
//       OrderingFeatures/LinearOrderingModel/IncrementalOrderingHeap
//                            cheap dynamic ordering infrastructure
//       StateElimination/    generic solver alive-graph and matrix-growth stats
//       ADT/                 ADTSimple tree/F-B/prefix-expression stats
//       ADTDelayed/          ADTDelayed-specific aggregation entry point
//
// Static neural models live under Static/ because they are meant for one-shot
// pre-solve scoring, not repeated online elimination decisions.
//
// ImportanceModel and ImportancePolicy remain at the root because they combine
// these facts rather than owning one particular profile family.
#include "Dataflow/APA/Importance/ImportanceModel.h"
#include "Dataflow/APA/Importance/ImportancePolicy.h"
#include "Dataflow/APA/Importance/Demand/DemandImportance.h"
#include "Dataflow/APA/Importance/Dynamic/ADT/ADTImportance.h"
#include "Dataflow/APA/Importance/Dynamic/ADT/ADTOrdering.h"
#include "Dataflow/APA/Importance/Dynamic/ADTDelayed/ADTDelayedImportance.h"
#include "Dataflow/APA/Importance/Dynamic/IncrementalOrderingHeap.h"
#include "Dataflow/APA/Importance/Dynamic/LinearOrderingModel.h"
#include "Dataflow/APA/Importance/Dynamic/OrderingFeatures.h"
#include "Dataflow/APA/Importance/Dynamic/StateElimination/DynamicImportance.h"
#include "Dataflow/APA/Importance/Sparse/SparseImportance.h"
#include "Dataflow/APA/Importance/Static/StaticImportance.h"

#endif // DATAFLOW_APA_IMPORTANCE_IMPORTANCE_H_
