// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "attrs.h"

namespace camel::tensor {

namespace attr = core::attr;

void registerTensorAttrKeys() {
    // Both values are Symbols naming an implementation: when two nodes are merged or a value is
    // granted twice, only an equal name keeps the attribute (the default merge rule). A key
    // registered here is what a dump, a cache and a Camel-written pass see.
    attr::registerKey(kKernelAttr, attr::KeySpec{.kind = attr::ValueKind::Symbol});
    attr::registerKey(kFusionAttr, attr::KeySpec{.kind = attr::ValueKind::Symbol});
}

attr::AttrKey kernelAttrKey() { return attr::key(kKernelAttr); }

attr::AttrKey fusionAttrKey() { return attr::key(kFusionAttr); }

bool usesLibraryGemm(attr::AttrSetId attrs) {
    const attr::AttrValueId value = attr::valueOf(attrs, kernelAttrKey());
    return value != attr::kNoValue && attr::value(value).text == kLibraryKernel;
}

} // namespace camel::tensor