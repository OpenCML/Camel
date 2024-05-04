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

#include "type.h"

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

type_ptr_t anyTypePtr;
type_ptr_t voidTypePtr;
type_ptr_t functorTypePtr;

const signed char primeTypeConvMatrix[7][7] = {
    // INT32, INT64, FLOAT, DOUBLE, STRING, BOOL, CHAR
    {01, 01, 01, 01, 00, 01, -1}, // INT32
    {-1, 01, -1, 01, 00, 01, -1}, // INT64
    {-1, -1, 01, 01, 00, 01, -1}, // FLOAT
    {-1, -1, -1, 01, 00, 01, -1}, // DOUBLE
    {00, 00, 00, 00, 01, 01, -1}, // STRING
    {01, 01, 01, 01, 01, 01, 01}, // BOOL
    {01, 01, 01, 01, 01, 01, 01}  // CHAR
};

std::string typeCodeToString(TypeCode code) {
    switch (code) {
        // primitive types
    case TypeCode::INT32:
        return "int32";
    case TypeCode::INT64:
        return "int64";
    case TypeCode::FLOAT:
        return "float";
    case TypeCode::DOUBLE:
        return "double";
    case TypeCode::STRING:
        return "string";
    case TypeCode::BOOL:
        return "bool";
    case TypeCode::CHAR:
        return "char";
        // structured types
    case TypeCode::SET:
        return "set";
    case TypeCode::MAP:
        return "map";
    case TypeCode::ARRAY:
        return "array";
    case TypeCode::LIST:
        return "list";
    case TypeCode::DICT:
        return "dict";
    case TypeCode::UNION:
        return "union";
    case TypeCode::VECTOR:
        return "vector";
    case TypeCode::MATRIX:
        return "matrix";
        // special types
    case TypeCode::ANY:
        return "any";
    case TypeCode::VOID:
        return "void";
    case TypeCode::FUNCTOR:
        return "functor";
    }
}

void initTypes() {
    // initialize primitive types
    int32TypePtr = std::make_shared<PrimeType>(TypeCode::INT32);
    int64TypePtr = std::make_shared<PrimeType>(TypeCode::INT64);
    floatTypePtr = std::make_shared<PrimeType>(TypeCode::FLOAT);
    doubleTypePtr = std::make_shared<PrimeType>(TypeCode::DOUBLE);
    stringTypePtr = std::make_shared<PrimeType>(TypeCode::STRING);
    boolTypePtr = std::make_shared<PrimeType>(TypeCode::BOOL);
    boolTypePtr = std::make_shared<PrimeType>(TypeCode::CHAR);

    // initialize structured types
    intTypePtr = std::make_shared<UnionType>(
        std::initializer_list<type_ptr_t>{int32TypePtr, int64TypePtr});
    realTypePtr = std::make_shared<UnionType>(
        std::initializer_list<type_ptr_t>{floatTypePtr, doubleTypePtr});
    numberTypePtr =
        std::make_shared<UnionType>(std::initializer_list<type_ptr_t>{
            int32TypePtr, int64TypePtr, floatTypePtr, doubleTypePtr});

    // initialize special types
    anyTypePtr = std::make_shared<SpecialType>(TypeCode::ANY);
    voidTypePtr = std::make_shared<SpecialType>(TypeCode::VOID);
    functorTypePtr = std::make_shared<SpecialType>(TypeCode::FUNCTOR);
}
