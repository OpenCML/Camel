/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license:
 * you may not use this software except in compliance with the License.
 * You may obtain a copy of the License at:
 * [https://opensource.org/license/mit]
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions or
 * limitations under the License.
 *
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

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