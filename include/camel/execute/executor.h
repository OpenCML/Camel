// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Executor : public std::enable_shared_from_this<Executor> {
  protected:
    camel::core::context::context_ptr_t context_;

    std::unordered_map<std::string, operator_t> opsMap_;

  public:
    Executor(
        camel::core::context::context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : context_(ctx), opsMap_(ops) {};
    virtual ~Executor() = default;

    std::optional<operator_t> find(const std::string &uri);
    std::string getNameOfAnOperator(const operator_t &op);
};

using executor_ptr_t     = std::shared_ptr<Executor>;
using executor_factory_t = std::function<executor_ptr_t()>;

class ExecutorManager {
  private:
    camel::core::context::context_ptr_t context_;
    std::unordered_map<std::string, executor_factory_t> executorFactories;
    mutable std::unordered_map<std::string, executor_ptr_t> loadedExecutors;
    // find() loads an executor on the first use of its protocol, and parallel schedulers call
    // it from several workers at once. The mutex guards both maps.
    mutable std::shared_mutex loadedExecutorsMutex;
    bool factoryRegistered(const std::string &protocol) const;
    // The operators whose result may cross the bridge (signatureOf): a small, explicit table, so a
    // module opts in per operator instead of every operator having to say it cannot.
    mutable std::unordered_map<std::string, std::string> bridgeSignatures;

  public:
    // The executor manager is a member of its context, so its back-reference to that context must
    // not own it: an owning one would keep the context alive through the context's own member. The
    // factories it holds capture this same handle, so an executor a module registered does not own
    // the context either (Context::handle()). As in Module, the conversion belongs in the
    // constructor rather than at the construction site, which is Context::create alone.
    ExecutorManager(camel::core::context::context_ptr_t ctx)
        : context_(ctx ? ctx->handle() : camel::core::context::context_ptr_t{}) {};
    ~ExecutorManager() = default;
    void registerExecutorFactory(std::string name, executor_factory_t fact);

    std::optional<operator_t> find(const std::string &uri) const;
    /// find() for whoever must not import a module by asking: the operator when the executor of
    /// its protocol is loaded or registered already, nothing when finding it would import the
    /// module first (or when there is no such operator).
    std::optional<operator_t> findWithoutImport(const std::string &uri) const;
    std::string getNameOfAnOperator(const operator_t &op) const;

    /// The generic signature of an operator whose *result* may cross the bridge into a generated
    /// program as a runtime object (`tensor:ones` and its family), or nothing when the operator has
    /// not been declared bridgeable. The signature says which kind of value it returns -- the
    /// concrete type of one call is an instance fact and is described by the call site, because a
    /// result like `T[..]` depends on the type argument and on the shape argument.
    std::optional<std::string> signatureOf(const std::string &uri) const;

    /// Declares that the result of `uri` may cross the bridge, with the generic signature `sig`.
    /// Only the modules that offer such an operator call this; every other operator stays unknown
    /// to signatureOf() and the bridge refuses to carry its result.
    void declareBridgeSignature(std::string uri, std::string sig) const;
};

using exec_mgr_uptr_t = std::unique_ptr<ExecutorManager>;
