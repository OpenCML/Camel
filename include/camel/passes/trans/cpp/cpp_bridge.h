#pragma once

#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/executor.h"
#include "camel/utils/dll_path.h"

#include <filesystem>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::execute::cppbridge {

using Type          = camel::core::type::Type;
using context_ptr_t = camel::core::context::context_ptr_t;

struct ValueRef {
    slot_t slot;
    Type *type;
};

inline ValueRef value(Type *type, slot_t slot) { return ValueRef{slot, type}; }
inline ValueRef val(Type *type, slot_t slot) { return value(type, slot); }

template <typename T> inline ValueRef value(Type *type, T data) {
    return ValueRef{camel::core::rtdata::toSlot(data), type};
}
template <typename T> inline ValueRef val(Type *type, T data) { return value(type, data); }

template <typename T> inline Type *type_of();

template <> inline Type *type_of<camel::core::rtdata::Int32>() { return Type::Int32(); }
template <> inline Type *type_of<camel::core::rtdata::Int64>() { return Type::Int64(); }
template <> inline Type *type_of<camel::core::rtdata::Float32>() { return Type::Float32(); }
template <> inline Type *type_of<camel::core::rtdata::Float64>() { return Type::Float64(); }
template <> inline Type *type_of<camel::core::rtdata::Bool>() { return Type::Bool(); }
template <> inline Type *type_of<::String *>() { return Type::String(); }
template <> inline Type *type_of<slot_t>() { return Type::Void(); }

template <typename T> inline ValueRef value(T data) {
    using U = std::remove_cvref_t<T>;
    return ValueRef{camel::core::rtdata::toSlot(data), type_of<U>()};
}
template <typename T> inline ValueRef val(T data) { return value(data); }

class ValueArgsView final : public ArgsView {
  public:
    ValueArgsView(slot_t *slots, Type **types, size_t size)
        : slots_(slots), types_(types), size_(size) {}

    size_t size() const override { return size_; }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    camel::core::type::TypeCode code(size_t index) const override { return types_[index]->code(); }
    Type *type(size_t index) const override { return types_[index]; }

  private:
    slot_t *slots_;
    Type **types_;
    size_t size_;
};

struct ArgsScratch {
    std::vector<slot_t> slots;
    std::vector<Type *> types;

    ValueArgsView fill(std::initializer_list<ValueRef> values) {
        if (slots.size() < values.size()) {
            slots.resize(values.size());
            types.resize(values.size());
        }
        size_t index = 0;
        for (const auto &valueRef : values) {
            slots[index] = valueRef.slot;
            types[index] = valueRef.type;
            ++index;
        }
        return ValueArgsView(slots.data(), types.data(), values.size());
    }
};

class RuntimeBridge;

/// Basic initialization (context + search paths). Must be called at the start of main; repeated
/// calls are ignored.
void initialize(const std::string &entryDir = "");

/// Import a dependency module (.cmo). Call this after initialize.
void importModules(const std::vector<std::string> &modules);

/// Clean up the global singleton. Call this before main exits.
void finalize();

/// Get the global bridge; returns nullptr when uninitialized.
RuntimeBridge *getBridge();

/// Get the global bridge by reference; throws if uninitialized.
inline RuntimeBridge &bridge() {
    auto *p = getBridge();
    if (!p)
        throw std::runtime_error(
            "[libcamel] cppbridge not initialized: call initialize() and importModules() at start "
            "of main");
    return *p;
}

/// Return the standard module search paths, used by exported C++ executables (such as
/// cpp_gen.exe) to load .cmo files.
/// Here "executable" means the generated and compiled program itself, not the Camel interpreter.
/// When entryDir is empty, use the executable's directory; if that fails, fall back to
/// current_path().
inline std::vector<std::string> getStandardModuleSearchPaths(const std::string &entryDir = "") {
    namespace fs        = std::filesystem;
    std::string baseDir = entryDir;
    if (baseDir.empty()) {
        auto exeDir = camel::utils::getExecutableDirectory();
        baseDir     = exeDir.empty() ? fs::current_path().string() : exeDir.string();
    }
    auto searchPaths = camel::utils::buildModuleSearchPaths(baseDir);
    // When the executable lives under a subdirectory such as build/Release, add a compatibility
    // stdlib path rooted at the project directory.
    auto exeDir = camel::utils::getExecutableDirectory();
    if (exeDir.empty()) {
        exeDir = fs::current_path();
    }
    searchPaths.push_back(fs::absolute(exeDir.parent_path().parent_path() / "stdlib").string());
    return searchPaths;
}

/// Create a Context using the standard search paths and match camel-cli initialization.
inline camel::core::context::context_ptr_t
createContextWithStandardConfig(const std::string &entryDir = "") {
    using namespace camel::core::context;
    std::string baseDir = entryDir;
    if (baseDir.empty()) {
        auto exeDir = camel::utils::getExecutableDirectory();
        baseDir     = exeDir.empty() ? std::filesystem::current_path().string() : exeDir.string();
    }
    return Context::create(
        EntryConfig{
            .entryDir    = baseDir,
            .entryFile   = "",
            .searchPaths = getStandardModuleSearchPaths(baseDir),
        },
        camel::core::error::DiagsConfig{});
}

class BoundOperator {
  public:
    BoundOperator() = default;
    BoundOperator(RuntimeBridge *owner, operator_t function) : owner_(owner), function_(function) {}

    template <typename Ret>
    Ret call(
        std::initializer_list<ValueRef> normArgs = {},
        std::initializer_list<ValueRef> withArgs = {}) const;

  private:
    RuntimeBridge *owner_ = nullptr;
    operator_t function_;
};

class RuntimeBridge {
  public:
    explicit RuntimeBridge(context_ptr_t context) : context_(std::move(context)) {}

    static RuntimeBridge create(const std::vector<std::string> &modules = {}) {
        auto context = camel::core::context::Context::create();
        for (const auto &module : modules) {
            if (!module.empty()) {
                context->importModule(module);
            }
        }
        return RuntimeBridge(std::move(context));
    }

    /// Create with standard module search paths, matching camel-cli, so .cmo dynamic modules load
    /// correctly. When entryDir is empty, use the executable's directory.
    static RuntimeBridge createWithStandardConfig(
        const std::vector<std::string> &modules, const std::string &entryDir = "") {
        auto context = createContextWithStandardConfig(entryDir);
        for (const auto &module : modules) {
            if (!module.empty()) {
                context->importModule(module);
            }
        }
        return RuntimeBridge(std::move(context));
    }

    ValueArgsView prepareNormArgs(std::initializer_list<ValueRef> values) {
        return normScratch_.fill(values);
    }

    ValueArgsView prepareWithArgs(std::initializer_list<ValueRef> values) {
        return withScratch_.fill(values);
    }

    BoundOperator &bind(const std::string &uri) {
        auto it = boundOperators_.find(uri);
        if (it != boundOperators_.end()) {
            return it->second;
        }
        auto op = context_->execMgr().find(uri);
        if (!op.has_value()) {
            throw std::runtime_error(std::string("generated runtime helper not found: ") + uri);
        }
        auto [inserted, _] = boundOperators_.emplace(uri, BoundOperator(this, *op));
        return inserted->second;
    }

    template <typename Ret>
    Ret call(
        const std::string &uri, std::initializer_list<ValueRef> normArgs,
        std::initializer_list<ValueRef> withArgs = {}) const {
        return const_cast<RuntimeBridge *>(this)->bind(uri).template call<Ret>(normArgs, withArgs);
    }

    context_ptr_t context() const { return context_; }

  private:
    context_ptr_t context_;
    ArgsScratch normScratch_;
    ArgsScratch withScratch_;
    std::unordered_map<std::string, BoundOperator> boundOperators_;
};

template <typename Ret>
Ret BoundOperator::call(
    std::initializer_list<ValueRef> normArgs, std::initializer_list<ValueRef> withArgs) const {
    auto normView = owner_->prepareNormArgs(normArgs);
    auto withView = owner_->prepareWithArgs(withArgs);
    return camel::core::rtdata::fromSlot<Ret>(function_(withView, normView, *owner_->context()));
}

} // namespace camel::execute::cppbridge
