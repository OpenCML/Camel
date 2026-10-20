#pragma once

// The DLPack C ABI types live in the core now (camel/core/array/dlpack.h), next to the
// zero-copy import and export. The tensor module keeps its spelling in camel::tensor.
#include "camel/core/array/dlpack.h"

namespace camel::tensor {

using namespace camel::core::array;

} // namespace camel::tensor