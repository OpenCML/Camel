/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 9, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "init.h"
#include "struct/type.h"


scope_ptr_t globalRootScope;

void initGlobalRootScope() {
    globalRootScope = std::make_shared<Scope<std::string, value_ptr_t>>();
    auto scope = globalRootScope;

    scope->insert("any",
                  make_value_ptr(make_type_ptr(PrimeType::ANY), std::any()));
    basicTypeMap["any"] = make_type_ptr(PrimeType::ANY);
    basicTypeMap["void"] = make_type_ptr(PrimeType::VOID);
    basicTypeMap["dict"] = make_type_ptr(PrimeType::DICT);
    basicTypeMap["list"] = make_type_ptr(PrimeType::LIST);
    basicTypeMap["union"] = make_type_ptr(PrimeType::UNION);
    basicTypeMap["int32"] = make_type_ptr(PrimeType::INT32);
    basicTypeMap["int64"] = make_type_ptr(PrimeType::INT64);
    basicTypeMap["float"] = make_type_ptr(PrimeType::FLOAT);
    basicTypeMap["double"] = make_type_ptr(PrimeType::DOUBLE);
    basicTypeMap["string"] = make_type_ptr(PrimeType::STRING);
    basicTypeMap["vector"] = make_type_ptr(PrimeType::VECTOR);
    basicTypeMap["matrix"] = make_type_ptr(PrimeType::MATRIX);
    basicTypeMap["boolean"] = make_type_ptr(PrimeType::BOOLEAN);
    basicTypeMap["functor"] = make_type_ptr(PrimeType::FUNCTOR);

    basicTypeMap["int"] = std::make_shared<UnionType>(
        std::vector<type_ptr_t>{basicTypeMap["int32"], basicTypeMap["int64"]});

    basicTypeMap["real"] = std::make_shared<UnionType>(
        std::vector<type_ptr_t>{basicTypeMap["float"], basicTypeMap["double"]});

    basicTypeMap["number"] = std::make_shared<UnionType>(
        std::vector<type_ptr_t>{basicTypeMap["int"], basicTypeMap["real"]});
};