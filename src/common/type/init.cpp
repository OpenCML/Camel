/**
 * Copyright (c) 2024 Beijing Jiaotong University
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 06, 2024
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "type.h"

#include "primary.h"
#include "special/functor.h"

using namespace std;

type_ptr_t int32TypePtr;
type_ptr_t int64TypePtr;
type_ptr_t floatTypePtr;
type_ptr_t doubleTypePtr;
type_ptr_t stringTypePtr;
type_ptr_t boolTypePtr;
type_ptr_t charTypePtr;

type_ptr_t intTypePtr;
type_ptr_t realTypePtr;
type_ptr_t numberTypePtr;

type_ptr_t listTypePtr;

type_ptr_t anyTypePtr;
type_ptr_t voidTypePtr;
type_ptr_t functorTypePtr;

type_ptr_t refTypePtr;

void initTypes() {
    // initialize primitive types
    int32TypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::INT32));
    int64TypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::INT64));
    floatTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::FLOAT));
    doubleTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::DOUBLE));
    stringTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::STRING));
    boolTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::BOOL));
    charTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimaryType>(TypeCode::CHAR));

    // initialize alias types
    intTypePtr = int32TypePtr;
    realTypePtr = floatTypePtr;
    numberTypePtr = doubleTypePtr;

    // initialize structured types
    listTypePtr = dynamic_pointer_cast<Type>(make_shared<ListType>());

    // initialize special types
    anyTypePtr = dynamic_pointer_cast<Type>(make_shared<SpecialType>(TypeCode::ANY));
    voidTypePtr = dynamic_pointer_cast<Type>(make_shared<SpecialType>(TypeCode::VOID));
    functorTypePtr = dynamic_pointer_cast<Type>(make_shared<FunctorType>(nullptr, nullptr, anyTypePtr));

    // initialize unknown type
    refTypePtr = dynamic_pointer_cast<Type>(make_shared<SpecialType>(TypeCode::REF));
}
