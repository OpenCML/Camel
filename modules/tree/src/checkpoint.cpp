/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Type-directed state persistence: own bytes before IO, validate all files before rebuilding.
#include "include/tree/checkpoint.h"
#include "../../tensor/binary.h"
#include "../../tensor/npy.h"
#include "../../tensor/type.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/tree.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/path.h"
#include "camel/utils/sha256.h"
#include "include/tree/checkpoint_io.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace camel::treelib {
namespace {
using namespace core::type;
using namespace core::rtdata;
using core::context::Context;
namespace mm = core::mm;
using Json   = nlohmann::json;
using tensor::Array;
using utils::sha256Hex;

// What a tree and a leaf are, and how a path is written: core::type::tree.
using core::type::tree::arity;
using core::type::tree::childName;
using core::type::tree::childPath;
using core::type::tree::childType;
using core::type::tree::displayPath;
using core::type::tree::isInternal;
[[noreturn]] void mismatch(const std::string &path, const std::string &reason) {
    throw std::invalid_argument(displayPath(path) + ": " + reason);
}
std::string kind(Type *type) {
    if (tensor::asTensorType(type))
        return "tensor";
    if (isBytesType(type))
        return "bytes";
    switch (type->code()) {
    case TypeCode::Struct:
        return "struct";
    case TypeCode::Tuple:
        if (isInternal(type))
            return "tuple";
        break;
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "f64";
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return "int";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::String:
        return "string";
    default:
        break;
    }
    return "unsupported";
}
struct NativeNode {
    Json spec;
    std::vector<NativeNode> children;
    std::string payload;
};
std::string hex(uint64_t bits, size_t width) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(static_cast<int>(width)) << bits;
    return text.str();
}
uint64_t bitsOf(const std::string &text, size_t width, const std::string &path) {
    uint64_t value          = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.size() != width || error != std::errc() || end != text.data() + text.size())
        mismatch(path, "invalid IEEE bit encoding");
    return value;
}
template <typename T> T integerOf(const std::string &text, const std::string &path) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
        mismatch(path, "invalid integer encoding");
    return value;
}

NativeNode snapshot(Type *type, slot_t value, const std::string &path, CheckpointFiles &files) {
    NativeNode node;
    const auto tag = kind(type);
    node.spec      = {{"kind", tag}, {"path", path}};
    if (tag == "unsupported")
        mismatch(path, "unsupported checkpoint leaf type " + type->toString());
    if (isInternal(type)) {
        node.spec["members"] = Json::array();
        for (size_t i = 0; i < arity(type); ++i) {
            const auto name    = childName(type, i);
            const slot_t child = type->code() == TypeCode::Struct
                                     ? fromSlot<::Struct *>(value)->get<slot_t>(i)
                                     : fromSlot<::Tuple *>(value)->get<slot_t>(i);
            auto nested = snapshot(childType(type, i), child, childPath(path, type, i), files);
            node.spec["members"].push_back({{"name", name}, {"type", std::move(nested.spec)}});
        }
        return node;
    }
    std::string extension = ".txt";
    if (tag == "tensor") {
        const auto t       = fromSlot<Array *>(value);
        const auto bytes   = tensor::tensorToLittleEndian(t);
        node.payload       = tensor::encodeNpy(t->dtype(), t->shapeSpan(), bytes);
        node.spec["dtype"] = tensor::dtypeName(t->dtype());
        node.spec["shape"] = t->shapeVector();
        extension          = ".npy";
    } else if (tag == "f32")
        node.payload = hex(std::bit_cast<uint32_t>(fromSlot<float>(value)), 8);
    else if (tag == "f64")
        node.payload = hex(std::bit_cast<uint64_t>(fromSlot<double>(value)), 16);
    else if (tag == "int")
        node.payload = std::to_string(fromSlot<int64_t>(value));
    else if (tag == "i32")
        node.payload = std::to_string(fromSlot<int32_t>(value));
    else if (tag == "bool")
        node.payload = fromSlot<bool>(value) ? "true" : "false";
    else if (tag == "string") {
        node.payload = std::string(fromSlot<::String *>(value)->view());
        if (invalidUtf8Offset(node.payload))
            mismatch(path, "string leaf is not UTF-8");
    } else if (tag == "bytes") {
        node.payload = std::string(fromSlot<BytesObject *>(value)->view());
        extension    = ".bin";
    }
    const auto file     = (path.empty() ? "_root" : path) + extension;
    node.spec["file"]   = file;
    node.spec["bytes"]  = node.payload.size();
    node.spec["sha256"] = sha256Hex(node.payload);
    files.emplace_back(file, std::move(node.payload));
    return node;
}

NativeNode readNode(
    Type *type, const Json &spec, const std::filesystem::path &directory, const std::string &path) {
    NativeNode node;
    node.spec      = spec;
    const auto tag = kind(type);
    if (tag == "unsupported" || spec.at("kind") != tag || spec.at("path") != path)
        mismatch(path, "checkpoint type structure differs from requested type");
    if (isInternal(type)) {
        const auto &members = spec.at("members");
        if (!members.is_array() || members.size() != arity(type))
            mismatch(path, "member count differs");
        for (size_t i = 0; i < arity(type); ++i) {
            const auto name = childName(type, i);
            if (members[i].at("name") != name)
                mismatch(childPath(path, type, i), "member name or order differs");
            node.children.push_back(readNode(
                childType(type, i),
                members[i].at("type"),
                directory,
                childPath(path, type, i)));
        }
        return node;
    }
    const auto extension = tag == "tensor" ? ".npy" : tag == "bytes" ? ".bin" : ".txt";
    const auto name      = (path.empty() ? "_root" : path) + extension;
    if (spec.at("file") != name)
        mismatch(path, "leaf filename differs from its type path");
    node.payload = readCheckpointFile(directory, name);
    if (spec.at("bytes").get<uint64_t>() != node.payload.size() ||
        spec.at("sha256") != sha256Hex(node.payload))
        mismatch(path, "leaf length or SHA-256 mismatch");
    if (tag == "tensor") {
        const auto target  = tensor::asTensorType(type);
        const auto shape   = spec.at("shape").get<tensor::Shape>();
        const auto dtype   = spec.at("dtype").get<std::string>();
        const auto storage = [&] {
            try {
                return tensor::inspectNpyExact(node.payload);
            } catch (const std::exception &error) {
                mismatch(path, error.what());
            }
        }();
        if (std::string(tensor::dtypeName(storage.dtype)) != dtype || storage.shape != shape)
            mismatch(path, "NPY dtype or shape differs from manifest");
        if (target->dtype() && tensor::dtypeName(*target->dtype()) != dtype)
            mismatch(path, "tensor dtype differs");
        if (target->shape()) {
            const auto &want = *target->shape();
            if (want.size() != shape.size())
                mismatch(path, "tensor rank differs");
            for (size_t i = 0; i < want.size(); ++i)
                if (want[i] != tensor::kUnknownDim && want[i] != shape[i])
                    mismatch(path, "tensor extent differs at axis " + std::to_string(i));
        }
    } else if (tag == "string" && invalidUtf8Offset(node.payload))
        mismatch(path, "string leaf is not UTF-8");
    else if (tag == "bool" && node.payload != "true" && node.payload != "false")
        mismatch(path, "invalid bool encoding");
    else if (tag == "f32")
        (void)bitsOf(node.payload, 8, path);
    else if (tag == "f64")
        (void)bitsOf(node.payload, 16, path);
    else if (tag == "int")
        (void)integerOf<int64_t>(node.payload, path);
    else if (tag == "i32")
        (void)integerOf<int32_t>(node.payload, path);
    return node;
}

slot_t rebuild(Type *type, const NativeNode &node) {
    const auto tag = kind(type);
    if (isInternal(type)) {
        const auto object = type->code() == TypeCode::Struct
                                ? toSlot(::Struct::create(arity(type), mm::autoSpace()))
                                : toSlot(::Tuple::create(arity(type), mm::autoSpace()));
        for (size_t i = 0; i < arity(type); ++i) {
            const auto child = rebuild(childType(type, i), node.children[i]);
            if (type->code() == TypeCode::Struct)
                fromSlot<::Struct *>(object)->set<slot_t>(
                    i,
                    child,
                    static_cast<StructType *>(type));
            else
                fromSlot<::Tuple *>(object)->set<slot_t>(i, child, static_cast<TupleType *>(type));
        }
        return object;
    }
    const auto path = node.spec.at("path").get<std::string>();
    if (tag == "tensor") {
        auto decoded = tensor::decodeNpyExact(node.payload, mm::autoSpace());
        if (node.spec.at("dtype") != std::string(tensor::dtypeName(decoded->dtype())) ||
            node.spec.at("shape") != decoded->shapeVector())
            mismatch(path, "NPY dtype or shape differs from manifest");
        return toSlot(decoded);
    }
    if (tag == "f32")
        return toSlot(std::bit_cast<float>(static_cast<uint32_t>(bitsOf(node.payload, 8, path))));
    if (tag == "f64")
        return toSlot(std::bit_cast<double>(bitsOf(node.payload, 16, path)));
    if (tag == "int")
        return toSlot(integerOf<int64_t>(node.payload, path));
    if (tag == "i32")
        return toSlot(integerOf<int32_t>(node.payload, path));
    if (tag == "bool")
        return toSlot(node.payload == "true");
    if (tag == "string")
        return toSlot(::String::from(node.payload, mm::autoSpace()));
    return toSlot(BytesObject::from(node.payload, mm::autoSpace()));
}

slot_t saveKernel(ArgsView &, ArgsView &norm, Context &) {
    const std::string directory(norm.get<::String *>(0)->view());
    const std::filesystem::path directoryPath = camel::utils::pathFromUtf8(directory);
    const std::string dataHash =
        norm.size() == 3 ? std::string(norm.get<::String *>(2)->view()) : "";
    try {
        if (directory.empty() || directory.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid checkpoint directory");
        CheckpointFiles files;
        const auto root = snapshot(norm.type(1), norm.slot(1), "", files);
        Json manifest{
            {"schema", 1},
            {"camel_version", CAMEL_CHECKPOINT_VERSION},
            {"dataset_manifest_sha256", dataHash},
            {"tree", root.spec}};
        files.emplace_back("manifest.json", manifest.dump(2) + "\n");
        publishCheckpoint(directoryPath, files);
        return NullSlot;
    } catch (const std::exception &error) {
        core::error::throwRuntimeFault(
            core::error::RuntimeDiag::FileSystemError,
            "ckpt.save_tree",
            directory,
            error.what());
    }
}
slot_t loadKernel(ArgsView &, ArgsView &norm, Context &) {
    const std::string directory(norm.get<::String *>(0)->view());
    const std::filesystem::path directoryPath = camel::utils::pathFromUtf8(directory);
    const std::string dataHash =
        norm.size() == 2 ? std::string(norm.get<::String *>(1)->view()) : "";
    try {
        if (directory.empty() || directory.find('\0') != std::string::npos)
            throw std::invalid_argument("invalid checkpoint directory");
        const auto output = asFallible(norm.resultType());
        if (!output)
            throw std::invalid_argument(
                "backend did not supply the static checkpoint result layout");
        // A save interrupted between its two renames leaves the complete previous checkpoint
        // in the hidden sibling `.<name>.ckpt-previous`: read that instead of failing, and say
        // so (the next save restores it before replacing it).
        bool recovered           = false;
        const auto readDirectory = checkpointReadDirectory(directoryPath, recovered);
        if (recovered)
            std::cerr << "warning: ckpt.load_tree: '" << directory
                      << "' is missing; reading the previous checkpoint an interrupted "
                         "save_tree left at '"
                      << camel::utils::pathToUtf8(readDirectory) << "'\n";
        const auto manifest = Json::parse(readCheckpointFile(readDirectory, "manifest.json"));
        if (manifest.at("schema") != 1)
            throw std::invalid_argument("unsupported checkpoint schema");
        if (!manifest.at("camel_version").is_string())
            throw std::invalid_argument("missing Camel version");
        const auto restored =
            readNode(output->payloadType(), manifest.at("tree"), readDirectory, "");
        if (!dataHash.empty() && manifest.at("dataset_manifest_sha256") != dataHash)
            std::cerr
                << "warning: ckpt.load_tree: dataset manifest SHA-256 differs from checkpoint\n";
        mm::NoCollectionScope guard;
        return makeOk(rebuild(output->payloadType(), restored), output);
    } catch (const std::exception &error) {
        return makeError("CheckpointError", directory + ": " + error.what(), "<ckpt::load_tree>");
    }
}
// A leaf type no checkpoint can hold is a compile-time error that names the path (design
// principles 13.5); a type variable is decided when the function is instantiated.
bool supportedLeaf(Type *leaf) { return containsTypeVar(leaf) || kind(leaf) != "unsupported"; }
std::optional<std::string> unsupportedLeaf(Type *tree) {
    if (const auto bad = core::type::tree::firstRejectedLeaf(tree, supportedLeaf))
        return "tree leaf '" + displayPath(bad->first) + "' has type " + bad->second->toString() +
               ", which a checkpoint cannot hold (tensor, Bytes, string, bool, i32, int, f32, f64)";
    return std::nullopt;
}
class SaveResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm, const ModifierSet &mods) const override {
        if (!fits(with, norm) || unsupportedLeaf(norm[1]))
            return std::nullopt;
        param_vec_t args;
        for (auto t : norm)
            args.emplace_back(t, false);
        return FunctionType::create({}, args, Type::Void(), mods);
    }
    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, static_args_t,
        const ModifierSet &) const override {
        return fits(with, norm) ? unsupportedLeaf(norm[1]) : std::nullopt;
    }
    std::string signature() const override {
        return "(dir: string, tree: T, dataset_manifest_sha256?: string) => void";
    }

  private:
    static bool fits(const type_vec_t &with, const type_vec_t &norm) {
        return with.empty() && (norm.size() == 2 || norm.size() == 3) &&
               norm[0]->code() == TypeCode::String &&
               (norm.size() == 2 || norm[2]->code() == TypeCode::String);
    }
};
class LoadResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *>
    resolve(const type_vec_t &, const type_vec_t &, const ModifierSet &) const override {
        return std::nullopt;
    }
    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expected, const type_vec_t &with, const type_vec_t &norm,
        static_args_t, const ModifierSet &mods) const override {
        Type *target = targetOf(types, expected);
        if (!fits(with, norm) || !target || containsTypeVar(target) || unsupportedLeaf(target))
            return std::nullopt;
        param_vec_t args;
        for (auto t : norm)
            args.emplace_back(t, false);
        return FunctionType::create({}, args, TupleType::fallible(target), mods);
    }
    std::optional<std::string> explainCallRejection(
        const type_vec_t &types, Type *expected, const type_vec_t &with, const type_vec_t &norm,
        static_args_t, const ModifierSet &) const override {
        Type *target = targetOf(types, expected);
        return fits(with, norm) && target && !containsTypeVar(target) ? unsupportedLeaf(target)
                                                                      : std::nullopt;
    }
    std::string signature() const override {
        return "::<T>(dir: string, dataset_manifest_sha256?: string) => T!";
    }

  private:
    static bool fits(const type_vec_t &with, const type_vec_t &norm) {
        return with.empty() && !norm.empty() && norm.size() <= 2 &&
               std::all_of(norm.begin(), norm.end(), [](Type *t) {
                   return t->code() == TypeCode::String;
               });
    }
    static Type *targetOf(const type_vec_t &types, Type *expected) {
        Type *target = types.size() == 1 ? types[0] : types.empty() ? expected : nullptr;
        if (types.empty())
            if (const auto fallible = asFallible(target))
                target = fallible->payloadType();
        return target;
    }
};
} // namespace

std::vector<oper_group_ptr_t> checkpointOperatorGroups() {
    return {
        OperatorGroup::create("load_tree", {{"ckpt:load_tree", std::make_shared<LoadResolver>()}}),
        OperatorGroup::create("save_tree", {{"ckpt:save_tree", std::make_shared<SaveResolver>()}}),
    };
}
std::unordered_map<std::string, operator_t> checkpointKernels() {
    return {{"save_tree", saveKernel}, {"load_tree", loadKernel}};
}
void declareCheckpointResources() {
    auto &table = core::OperatorTable::instance();
    table.declare("ckpt", {{"save_tree", false}, {"load_tree", false}});
    auto reader = core::sem::externalAmbient("fs", core::sem::AccessMode::Read);
    reader.accesses.push_back(core::sem::ambientAccess("io:stderr", core::sem::AccessMode::Write));
    table.declareResources("ckpt:load_tree", std::move(reader));
    table.declareResources(
        "ckpt:save_tree",
        core::sem::externalAmbient("fs", core::sem::AccessMode::ReadModifyWrite));
}
} // namespace camel::treelib
