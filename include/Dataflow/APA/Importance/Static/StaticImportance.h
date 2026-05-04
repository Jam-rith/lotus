#ifndef DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCE_H_
#define DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCE_H_

// Static importance is computed once from the input CFG plus optional
// reducibility/dominator hooks. It records structural facts only; it must not
// inspect the current path-expression matrix or elimination alive set.
#include "Dataflow/APA/Importance/Static/StaticImportanceBuilder.h"
#include "Dataflow/APA/Importance/Static/StaticImportanceProfile.h"

#endif // DATAFLOW_APA_IMPORTANCE_STATIC_STATICIMPORTANCE_H_
