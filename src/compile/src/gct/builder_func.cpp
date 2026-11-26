// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the lowering of a function or lambda -- its body, the closure captures, and the generator machine a body writing `yield` becomes.

#include "builder.h"
#include <exception>

#include "camel/compile/semantic_recorder.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/module/userdef.h"
#include "camel/core/operator_names.h"
#include "camel/core/tags.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/native.h"
#include "camel/core/type/other.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typecall.h"
#include "camel/core/type/typevar.h"
#include "camel/parse/ast/type.h"
#include "camel/utils/escape.h"
#include "camel/utils/number_text.h"
#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"
#include <stdexcept>

#include "builder_internal.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gct {



namespace {

/// Whether a body writes a `yield` of its own: not one of a function or a lambda written in it,
/// whose body is a body of its own (language reference 7.3.1).
bool writesOwnYield(const AST::node_ptr_t &node);
/// The first `yield` a body writes of its own (`nullptr` when it writes none): the position a
/// refusal names, so that what the reader sees is the `yield` that is not lowered yet.
AST::node_ptr_t firstOwnYield(const AST::node_ptr_t &node);

} // namespace

/*
FuncData(Ref ref) : FuncType funcType, StmtBlock body ;
*/
node_ptr_t Builder::visitFuncData(const AST::node_ptr_t &ast) {
    ENTER("FuncData");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for FuncData");
    const auto &funcData = ast->loadAs<AST::FuncDataLoad>();
    // The type variables a declaration binds are in scope in the rest of its signature and in
    // its body, and nowhere else.
    const bool isDecl = std::exchange(funcDeclPending_, false);
    struct TypeScopeGuard {
        Builder &self;
        scope_ptr_t<Reference, Type *> outer;
        std::vector<std::string> outerVars;
        std::vector<std::string> outerPacks;
        std::shared_ptr<std::unordered_map<std::string, Type *>> outerHead;
        std::unordered_set<std::string> outerWritten;
        bool outerSignatureVisit;
        ~TypeScopeGuard() {
            self.typeScope_        = outer;
            self.declaredTypeVars_ = std::move(outerVars);
            self.declaredPackVars_ = std::move(outerPacks);
            self.headTypeVarTypes_ = std::move(outerHead);
            self.writtenHeadVars_  = std::move(outerWritten);
            self.signatureVisit_   = outerSignatureVisit;
        }
    } typeScopeGuard{
        *this,
        typeScope_,
        std::exchange(declaredTypeVars_, {}),
        std::exchange(declaredPackVars_, {}),
        std::move(headTypeVarTypes_),
        std::move(writtenHeadVars_),
        signatureVisit_};
    pushScope();
    // The declaration's head names (`func first::<T>(...)`) are the occurrence of each type
    // variable in this signature; visitFuncType puts them into the scope it just pushed.
    headTypeVarTypes_ = std::make_shared<std::unordered_map<std::string, Type *>>();
    writtenHeadVars_.clear();
    signatureVisit_ = true;
    declSignature_  = isDecl;
    openSignature_  = true;
    Tree::ParameterDefaults defaults;
    FunctionType *funcType =
        tt::as_ptr<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0), &defaults));
    // The head parameters the signature never wrote; the body may still use them (`match type
    // T`), so the warning waits until the body is built.
    const std::vector<std::string> unusedHeadVars = std::exchange(pendingUnusedHeadVars_, {});
    const auto reportUnusedHead = [&](const node_ptr_t &body, const node_ptr_t &when) {
        for (const std::string &name : unusedHeadVars) {
            if (usesTypeVar(body, name) || usesTypeVar(when, name)) {
                continue;
            }
            diags_->of(SemanticDiag::TypeVariableUnused)
                .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                .commit(
                    name,
                    "the declaration declares it at its head and its signature never writes it");
        }
    };
    signatureVisit_ = false;
    // The parameters of a lambda whose type was left out (design principles 5.9), filled by GIR
    // from the function type the lambda stands in.
    const std::vector<Tree::UnwrittenParam> unwrittenParams =
        std::exchange(pendingUnwrittenParams_, {});
    // A parameter destructured as a tuple (`((a, b)) => ...`) is bound by GIR when it starts the
    // body: the parameter is one value, and the names take its elements.
    std::vector<Tree::TupleParamBinding> tupleParams;
    {
        size_t index = 0;
        for (size_t group = 0; group < 2; ++group) {
            for (const auto &paramPair :
                 *ast->atAs<AST::FuncTypeLoad>(0)->atAs<AST::RepeatedLoad>(group)) {
                const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
                if (paramLoad->isTupleParam()) {
                    tupleParams.push_back({index, paramLoad->tupleNames()});
                }
                ++index;
            }
        }
    }
    // A parameter of a reserved name would stand in front of the operator of that name.
    for (size_t i = 0; i < funcType->argNamesCount(); ++i) {
        if (!validateIdent(std::string(funcType->argNameAt(i)))) {
            diags_->of(SemanticDiag::ReservedIdentifier)
                .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                .commit();
            throw BuildAbortException();
        }
    }
    if (!declaredTypeVars_.empty()) {
        // A head name written in the signature but dropped by type construction (the argument of
        // a type function that does not use its parameter) is written and still cannot be bound.
        std::vector<std::string> surviving;
        collectTypeVarsWithoutPlaceholders(funcType, surviving);
        for (const std::string &name : declaredTypeVars_) {
            if (writtenHeadVars_.contains(name) &&
                std::find(surviving.begin(), surviving.end(), name) == surviving.end()) {
                diags_->of(SemanticDiag::TypeVariableNotBindable)
                    .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                    .commit(
                        name,
                        funcData->ref().toString(),
                        "it is written only where type construction drops it, as the argument "
                        "of a type function that does not use it");
                throw BuildAbortException();
            }
        }
        // A variable must survive type construction in an argument or result position.
        std::vector<std::string> bindable;
        for (Type *param : funcType->withTypesSpan()) {
            collectBindableTypeVars(param, bindable);
        }
        for (Type *param : funcType->normTypesSpan()) {
            collectBindableTypeVars(param, bindable);
        }
        if (funcType->hasExitType()) {
            collectBindableTypeVars(funcType->exitType(), bindable);
        }
        for (const std::string &name : declaredTypeVars_) {
            if (std::find(bindable.begin(), bindable.end(), name) == bindable.end() &&
                writtenHeadVars_.contains(name)) {
                diags_->of(SemanticDiag::TypeVariableNotBindable)
                    .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                    .commit(
                        name,
                        funcData->ref().toString(),
                        "it occurs only inside a union, or as the argument of a type function "
                        "that does not use it");
                throw BuildAbortException();
            }
        }
    }
    node_ptr_t typeNode = tree_->makeType(funcType);
    // The applicability condition is built in the scope of the signature: it sees the type
    // parameters declared at the head and the `with` parameters (design principles 4.4). It is
    // not evaluated here -- instantiation is where its type parameters have types -- so it is
    // kept on the FUNC, beside the type, for the graph builder.
    node_ptr_t whenNode = nullptr;
    if (funcData->when() != nullptr) {
        whenNode = visitData(funcData->when());
    }
    // A declaration without a body declares an operator of a C++ module (design principles 6.4):
    // it has a signature and nothing to build, so the FUNC carries its type alone and is marked
    // as a declaration. The graph builder turns it into an operator group instead of a graph
    // (step 1) -- unless its condition is the pass applicability, in which case it
    // declares a pass and the pipeline reads it (design principles 9.3).
    const bool nativeDeclaration = ast->size() < 2;
    // A pack among the `with` parameters is allowed on a declaration with a body as on one without.
    // The pack rule is the one the ordinary parameters follow
    // (design principles 4.4, 5.6). Its binding of the call's `with` arguments, and what the body
    // may do with it, are built where the body is; here the signature keeps it as the `void` slot
    // `addWithRest` writes, which a call never passes a value for.
    // A `.cml` found beside a `.cmo` of the same name is that C++ module's *declaration*
    // module, in which the C++ operators are declared; it may also hold Camel definitions.
    // One module is the pair: declarations and Camel definitions in the `.cml`, the
    // native kernels they name in the `.cmo` (design principles 10.4).
    if (nativeDeclaration) {
        if (!funcType->hasExitType()) {
            diags_->of(SemanticDiag::NativeDeclarationIncomplete)
                .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                .commit(funcData->ref().toString());
            throw BuildAbortException();
        }
        node_ptr_t funcNode = tree_->makeFunc(funcData->ref().toString());
        funcNode->setNativeDeclaration();
        tree_->setHeadTypeParams(
            funcNode,
            ast->atAs<AST::FuncTypeLoad>(0)->loadAs<AST::FuncTypeLoad>()->typeParamNames());
        if (whenNode != nullptr) {
            tree_->setWhen(funcNode, {whenNode, funcData->whenText(), whenNode->origin()});
        }
        reportUnusedHead(nullptr, whenNode);
        if (!defaults.empty()) {
            tree_->setParameterDefaults(funcNode, defaults);
        }
        *funcNode << typeNode;
        setOriginFromAst(
            context_,
            typeNode,
            ast,
            camel::source::OriginKind::GctNode,
            "gct.func.type");
        setOriginFromAst(context_, funcNode, ast, camel::source::OriginKind::GctNode, "gct.func");
        registerGctSemantic(
            context_,
            funcNode,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::FuncName,
                    findSemanticOrigin(
                        astSemantic(context_, ast),
                        camel::source::SemanticRole::FuncName),
                    -1,
                    funcData->ref().toString()),
                semanticPart(
                    camel::source::SemanticRole::ReturnType,
                    nodeOrigin(typeNode),
                    -1,
                    "type"),
            },
            {nodeOrigin(typeNode)});
        LEAVE("FuncData");
        return funcNode;
    }
    // The dimension names the signature's parameter types bound are in scope in the body: a type
    // the body writes (`let out: f32[n] = ...`) names the extent those parameters bound, exactly
    // as the return type does (design principles 5.4). They are this signature's names and
    // nothing else: a name bound in another declaration is not visible here, and a nested
    // declaration or a nested function type pushes a scope of its own, so these are not visible
    // inside it either.
    const std::shared_ptr<std::set<std::string>> enclosingNames = dimNames_;
    struct BodyDimensionScope {
        Builder &self;
        std::shared_ptr<std::set<std::string>> outerNames;
        std::shared_ptr<std::set<std::string>> outerPacks;
        bool outerBindAllowed;
        ~BodyDimensionScope() {
            self.dimNames_       = std::move(outerNames);
            self.dimPacks_       = std::move(outerPacks);
            self.dimBindAllowed_ = outerBindAllowed;
        }
    } bodyDimensionScope{
        *this,
        std::exchange(dimNames_, std::make_shared<std::set<std::string>>()),
        std::exchange(dimPacks_, std::make_shared<std::set<std::string>>()),
        std::exchange(dimBindAllowed_, false)};
    for (const bool declared : {false, true}) {
        const std::span<Type *const> params =
            declared ? funcType->withTypesSpan() : funcType->normTypesSpan();
        for (Type *param : params) {
            if (param == nullptr) {
                continue;
            }
            // The names the parameter type writes are the ones it bound: a shape naming a
            // dimension, and the name of a pack (`f32[n, ..rest]`). A name the enclosing
            // signature already had is not this one's: a lambda nested in a body names it from
            // the outside, and its own body does not inherit it.
            std::vector<ExtentForm> forms;
            if (param->extentForms(forms)) {
                for (const ExtentForm &form : forms) {
                    for (const std::string &name : form.usedNames()) {
                        if (enclosingNames == nullptr || !enclosingNames->contains(name)) {
                            dimNames_->insert(name);
                        }
                    }
                }
            }
            if (!param->packName().empty()) {
                dimPacks_->insert(std::string(param->packName()));
            }
        }
    }
    // The body of a function or lambda is a body of its own: its value is the function's
    // result, and its exits are the function's, wherever the function is written.
    const ValueUseScope valueUseScope(*this);
    valueUse_                      = {};
    const AST::node_ptr_t &bodyAst = ast->atAs<AST::StmtBlockLoad>(1);
    node_ptr_t stmtsNode =
        writesOwnYield(bodyAst) ? visitGeneratorBody(ast, funcType) : visitStmtBlock(bodyAst);
    reportUnusedHead(stmtsNode, whenNode);
    node_ptr_t funcNode = tree_->makeFunc(funcData->ref().toString());
    tree_->setHeadTypeParams(
        funcNode,
        ast->atAs<AST::FuncTypeLoad>(0)->loadAs<AST::FuncTypeLoad>()->typeParamNames());
    *funcNode << typeNode << stmtsNode;
    if (whenNode != nullptr) {
        tree_->setWhen(funcNode, {whenNode, funcData->whenText(), whenNode->origin()});
    }
    if (!defaults.empty()) {
        tree_->setParameterDefaults(funcNode, defaults);
    }
    if (!unwrittenParams.empty()) {
        tree_->setUnwrittenParams(funcNode, unwrittenParams);
    }
    if (!tupleParams.empty()) {
        tree_->setTupleParams(funcNode, tupleParams);
    }
    setOriginFromAst(context_, typeNode, ast, camel::source::OriginKind::GctNode, "gct.func.type");
    setOriginFromAst(context_, funcNode, ast, camel::source::OriginKind::GctNode, "gct.func");
    registerGctSemantic(
        context_,
        funcNode,
        ast,
        {
            semanticPart(
                camel::source::SemanticRole::FuncName,
                findSemanticOrigin(
                    astSemantic(context_, ast),
                    camel::source::SemanticRole::FuncName),
                -1,
                funcData->ref().toString()),
            semanticPart(camel::source::SemanticRole::ReturnType, nodeOrigin(typeNode), -1, "type"),
            semanticPart(
                camel::source::SemanticRole::ValueProducer,
                nodeOrigin(stmtsNode),
                -1,
                "body"),
        },
        {nodeOrigin(typeNode), nodeOrigin(stmtsNode)});
    LEAVE("FuncData");
    return funcNode;
}

namespace {

/// Whether a body writes a `yield` of its own: not one of a function or a lambda written in it,
/// whose body is a body of its own (language reference 7.3.1).
bool writesOwnYield(const AST::node_ptr_t &node) {
    if (node == nullptr) {
        return false;
    }
    if (node->type() == AST::LoadType::Stmt) {
        const auto &load = node->loadAs<AST::StmtLoad>();
        if (load->stmtType() == AST::StmtType::Func) {
            return false;
        }
        if (load->stmtType() == AST::StmtType::Exit &&
            node->loadAs<AST::ExitStmtLoad>()->exitType() == AST::ExitType::Yield) {
            return true;
        }
    }
    if (node->type() == AST::LoadType::Data &&
        node->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Func) {
        return false;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (writesOwnYield(node->at(i))) {
            return true;
        }
    }
    return false;
}

/// Whether `node` reads the name `name` of its own: a function or a lambda written in it has a
/// body and locals of its own (language reference 7.3.1), and a declaration of the same name hides
/// this one from there on, so neither is read of it. Used for the one question the generator's
/// machine asks of a name: whether a `yield` cuts a local in two.
bool readsOwnName(const AST::node_ptr_t &node, const std::string &name) {
    if (node == nullptr) {
        return false;
    }
    if (node->type() == AST::LoadType::Stmt) {
        const auto &load = node->loadAs<AST::StmtLoad>();
        if (load->stmtType() == AST::StmtType::Func) {
            return false;
        }
        if (load->stmtType() == AST::StmtType::Data) {
            bool declares = false;
            for (const Reference &ref : node->loadAs<AST::DataDeclLoad>()->refs()) {
                if (ref.toString() == name) {
                    declares = true;
                }
            }
            if (declares) {
                // The initializer is read where this declaration stands, so it sees the name the
                // outer binding gave it; from the declaration on, the name is this one.
                for (size_t i = 1; i < node->size(); ++i) {
                    if (readsOwnName(node->at(i), name)) {
                        return true;
                    }
                }
                return false;
            }
        }
    }
    if (node->type() == AST::LoadType::Data) {
        const auto &load = node->loadAs<AST::DataLoad>();
        if (load->dataType() == AST::DataType::Ref &&
            node->loadAs<AST::RefDataLoad>()->ref().toString() == name) {
            return true;
        }
        if (load->dataType() == AST::DataType::Func) {
            return false;
        }
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (readsOwnName(node->at(i), name)) {
            return true;
        }
    }
    return false;
}

/// The reference `node` makes to the name `name`, or null where it makes none: `readsOwnName`
/// in the form the report needs, so that the error stands on the name and not on the statement
/// around it.
AST::node_ptr_t nameUseIn(const AST::node_ptr_t &node, const std::string &name) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->type() == AST::LoadType::Stmt) {
        const auto &load = node->loadAs<AST::StmtLoad>();
        if (load->stmtType() == AST::StmtType::Func) {
            return nullptr;
        }
        if (load->stmtType() == AST::StmtType::Data) {
            bool declares = false;
            for (const Reference &ref : node->loadAs<AST::DataDeclLoad>()->refs()) {
                if (ref.toString() == name) {
                    declares = true;
                }
            }
            if (declares) {
                for (size_t i = 1; i < node->size(); ++i) {
                    if (AST::node_ptr_t use = nameUseIn(node->at(i), name); use != nullptr) {
                        return use;
                    }
                }
                return nullptr;
            }
        }
    }
    if (node->type() == AST::LoadType::Data) {
        const auto &load = node->loadAs<AST::DataLoad>();
        if (load->dataType() == AST::DataType::Ref &&
            node->loadAs<AST::RefDataLoad>()->ref().toString() == name) {
            return node;
        }
        if (load->dataType() == AST::DataType::Func) {
            return nullptr;
        }
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (AST::node_ptr_t use = nameUseIn(node->at(i), name); use != nullptr) {
            return use;
        }
    }
    return nullptr;
}

AST::node_ptr_t firstOwnYield(const AST::node_ptr_t &node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->type() == AST::LoadType::Stmt) {
        const auto &load = node->loadAs<AST::StmtLoad>();
        if (load->stmtType() == AST::StmtType::Func) {
            return nullptr;
        }
        if (load->stmtType() == AST::StmtType::Exit &&
            node->loadAs<AST::ExitStmtLoad>()->exitType() == AST::ExitType::Yield) {
            return node;
        }
    }
    if (node->type() == AST::LoadType::Data &&
        node->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Func) {
        return nullptr;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (AST::node_ptr_t found = firstOwnYield(node->at(i))) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

/*
FuncData(Ref ref) : FuncType funcType, StmtBlock body
A function whose body writes `yield` hands out a stream (design principles 5.4, language
reference 7.3.1). Its body is not run where the function is called: the call answers the stream
value, and each pull runs the body on from where the pull before it stopped.

The machine is the one docs/design/io-concurrency-streams/plan.cn.md slice E describes, with the
state kept the way this language can keep it:
  - the `pc` cell says which point of the body the next pull starts from. That is the whole of the
    state's own part: whether the pull in hand is done is the round's flag and not the generator's
    (`roundState` below), and that flag is all the machine's `loop` stops on;
  - every local that lives across a `yield` is a cell of the function's own body. That is the
    machine's own bookkeeping and not the meaning of `var`: `var` is a name that may be bound
    again, and a closure captures the value the name held where the closure is written (design
    principles 3.5). A closure the body makes never gets the cell through a capture port, and a
    write of the name after the closure is made and before its last use is refused where it stands
    (`SemanticDiag::CapturedVarWrittenAfterTheClosure`, 198);
  - `step` is one function: a `loop` while the round it was handed has not answered, around a
    branch on `pc`. A stretch with
    no `yield` runs inside one round of that loop, so a `yield` is the only thing that leaves;
    the value of a round is the answer of the pull -- the element and the rest of the stream at a
    `yield`, the state it has had since the last one anywhere else -- and the value of the loop
    is the answer the pull hands out;
  - a cell is a node identity and not a type (a value taken out of a struct is not a variable),
    so the state is not one struct: it is the cells, and the machine's own arms hold the handles.
    No capture port carries one of them: a closure the body makes captures a value (3.5).
*/
node_ptr_t
Builder::visitGeneratorBody(const AST::node_ptr_t &funcAst, const FunctionType *funcType) {
    ENTER("GeneratorBody");
    const AST::node_ptr_t &bodyAst = funcAst->atAs<AST::StmtBlockLoad>(1);
    const auto &bodyStmts          = bodyAst->atAs<AST::RepeatedLoad>(0);

    // The head writes the stream type, and it is what says what one `yield` hands out. A statement
    // shape this machine does not build is a broken invariant, guarded where the statement walk
    // meets it -- not a refusal written for the reader, and not a property of any program.
    // The body of a generator `loop` is cut at its `yield`, and its last statement writes the next
    // state value. A body that does not is a rule the reader has to be told, not a defect.
    const auto reportLoopBodyShape = [&](const AST::node_ptr_t &atNode) -> void {
        diags_->of(SemanticDiag::LoopBodyNeedsNextState).at(atNode->load()->tokenRange()).commit();
        throw BuildAbortException();
    };
    // A local that lives across a `yield` is a cell of the body (see `addTypedCell`), so there is
    // no declaration shape that the machine cannot read, so this place has no refusal for
    // "a local read after a `yield`".
    Type *exit = funcType->exitType();
    if (exit == nullptr || exit->code() != TypeCode::Stream) {
        // The head is what is wrong: a body that yields hands out a stream, and this head writes
        // something else. Said at the head, not at the first `yield` the body happens to hold.
        const AST::node_ptr_t atYield = firstOwnYield(bodyAst);
        diags_->of(SemanticDiag::YieldRequiresStreamHead)
            .at((atYield != nullptr ? atYield : bodyAst)->load()->tokenRange())
            .commit();
        throw BuildAbortException();
    }
    auto *stream             = static_cast<StreamType *>(exit);
    Type *elemType           = stream->elemType();
    FunctionType *stepType   = stream->stepType();
    Type *answerType         = TupleType::nullable(TupleType::create({elemType, exit}));
    const AST::node_ptr_t at = bodyStmts->size() > 0 ? bodyStmts->at(0) : bodyAst;
    // The generator's head writes the `sync` of its body: the machine's own blocks take the same
    // one, so a write between two `yield`s is ordered by the same rule as any other (3.4).
    const bool synced = bodyAst->loadAs<AST::StmtBlockLoad>()->synced();

    // The cell the step unpacks out of the state: the 0th member is the point of the body the next
    // pull starts from. It belongs to the step and not to this body, and the name is used only where
    // such a cell is made, read or written: no decision below asks what a name is, so the name is
    // compared to nothing.
    // The state carries `pc` alone: a separate `halt` bit would be written where a round answered
    // and read only where the state was packed, so it would decide nothing, and the round's own
    // flag already says the pull in hand is done.
    const std::string pcCell = "__gen_pc__";
    // The step's state parameter. It is named, so the port `src/compile/gir/builder_helpers.h`
    // makes for it carries the same name and the step's body can read it.
    const std::string stateParamName = "__gen_state__";
    const std::string outName  = "__gen_out__";
    const std::string stepName = "__gen_step__";

    const auto intLiteral = [&](int64_t value) {
        return tree_->makeData(makeDataFromLiteral<int64_t>(value));
    };
    const auto mark = [&](const node_ptr_t &node, const AST::node_ptr_t &ast, const char *what) {
        setOriginFromAst(context_, node, ast, camel::source::OriginKind::GctNode, what);
        return node;
    };
    const auto readCell  = [&](const std::string &name) { return tree_->makeDRef(name); };
    const auto writeCell = [&](const std::string &name, node_ptr_t value) {
        node_ptr_t write = tree_->makeAssign(AssignmentForm::Plain);
        *write << readCell(name) << value;
        return write;
    };
    const auto equalTo = [&](const std::string &name, int64_t value) {
        node_ptr_t link = tree_->makeLink(2);
        *link << tree_->makeDRef("__eq__") << readCell(name) << intLiteral(value);
        return link;
    };
    // The stream value of one pull: a closure over the step function, holding the state this pull
    // hands to the next one. Its own type stays `stepType` -- that is what the program reads
    // (`for x in s`) -- and the state it carries is the machine's business.
    const auto streamValue = [&](node_ptr_t state) {
        node_ptr_t lambda = tree_->makeFunc(std::string());
        *lambda << tree_->makeType(stepType);
        node_ptr_t body = makeExec(synced);
        node_ptr_t call = tree_->makeLink(0);
        *call << tree_->makeDRef(stepName) << state;
        node_ptr_t ret = tree_->makeExit(ExitType::Return);
        *ret << call;
        *body << ret;
        *lambda << body;
        mark(lambda, at, "gct.generator.stream");
        return lambda;
    };
    // The element and the rest of the stream, as one value of `(T, T..)`.
    const auto pairValue = [&](node_ptr_t elem, node_ptr_t rest) {
        TupleDataFactory factory;
        bool dangling       = false;
        node_ptr_t operands = makeExec(false, true);
        for (const node_ptr_t &part : {elem, rest}) {
            auto [data, more] = extractData(part, operands, dangling);
            factory.add(data);
        }
        node_ptr_t node = tree_->makeData(factory.build());
        if (dangling) {
            *operands << node;
            node = operands;
        }
        return mark(node, at, "gct.generator.pair");
    };

    // The cells: every local of the body in the order it is written. The machine's own part -- the
    // point the next pull starts from -- is not among them: it is the state's own member (see
    // `stateType` below), and the step unpacks it into a cell of its own (`unpackSlot`). A cell here
    // would be a second copy of one fact, and nothing would read it: a name in an arm resolves to
    // the step's own local, which lets the unpacking stand ahead of the arms. A
    // declaration is hoisted only where what it holds does not depend on the point it stands at --
    // a constant or a lambda, which reads its cells when it is called and not now.
    node_ptr_t outer = makeExec(synced);
    // A cell of a value that is written where its statement stands: the cell's type position holds
    // `typeof <that value's expression>`, so the cell has a type and no value yet, and the body
    // that creates the stream evaluates nothing (design principles 5.4). The value is written
    // where the statement is reached, in the arm that reaches it.
    const auto addCellOfType =
        [&](const std::string &name, Type *type, const AST::node_ptr_t &ast) {
            node_ptr_t ref = tree_->makeNRef(name);
            *ref << tree_->makeType(type);
            node_ptr_t vari = tree_->makeVari();
            *vari << ref;
            mark(vari, ast, "gct.generator.cell");
            *outer << vari;
        };
    // The same cell, typed by the expression whose value it will hold.
    const auto addTypedCell =
        [&](const std::string &name, const node_ptr_t &expr, const AST::node_ptr_t &ast) {
            addCellOfType(name, typeOfPlaceholder(expr), ast);
        };
    // The body's declarations that became cells: the statement that writes each, where it stands
    // in `bodyStmts`, and the cell's name. A cell is written where its declaration stands, so an
    // arm that starts after that statement finds it written and a read that stands before it is
    // a name used before it is declared.
    struct HoistedCell {
        const AST::Node *decl;
        size_t index;
        std::string name;
    };
    std::vector<HoistedCell> hoisted;
    // A declaration that binds several names to one value: the cell the value is put in, and the
    // cells its elements are written into where the statement stands.
    struct Destructured {
        const AST::Node *decl;
        std::string tupleName;
        std::vector<std::string> names;
    };
    std::vector<Destructured> destructured;
    // A first value the loop would start from in any arm: a constant or a lambda, which reads its
    // cells when it is called and not now. A cell's value is written where the loop stands, so a
    // computed first value would be read there too -- and the arms a `yield` splits the loop into
    // are not lowered for that yet.
    const auto hoistable = [](const AST::node_ptr_t &init) {
        if (init == nullptr) {
            return true;
        }
        if (init->type() != AST::LoadType::Data) {
            return false;
        }
        const auto dataType = init->loadAs<AST::DataLoad>()->dataType();
        return dataType == AST::DataType::Literal || dataType == AST::DataType::Func ||
               dataType == AST::DataType::ConstAs;
    };
    // A name that is a cell of this body's own, and -- where the cell's value has to outlive the
    // pull -- the type of the state slot it travels in. `slotType` is decided where the cell is
    // declared, by the type the cell holds: a cell whose type has a zero value (`zeroValueOf`) can
    // be a slot, and the rest keep their cell, which is why the state is built beside the cells and
    // not out of all of them. Nothing below asks a name what it is: a cell with a slot is the
    // step's own local (every arm reads it there, and it needs no touch), and one without a slot
    // stays a cell of this body.
    struct CellDecl {
        std::string name;
        Type *slotType; // nullptr: a cell of this body and not a part of the state
    };
    std::vector<CellDecl> cells;
    const auto isCell = [&](const std::string &name) {
        return std::any_of(cells.begin(), cells.end(), [&](const CellDecl &cell) {
            return cell.name == name;
        });
    };
    const auto addSlotCell = [&](const std::string &name,
                                 const node_ptr_t &value,
                                 const AST::node_ptr_t &ast) {
        Type *type = typeOfPlaceholder(value);
        addCellOfType(name, type, ast);
        cells.push_back({name, zeroValueOf(type) != nullptr ? type : nullptr});
    };
    for (size_t i = 0; i < bodyStmts->size(); ++i) {
        const auto &stmt = bodyStmts->at(i);
        if (stmt->type() != AST::LoadType::Stmt ||
            stmt->loadAs<AST::StmtLoad>()->stmtType() != AST::StmtType::Data) {
            continue;
        }
        const auto &decl  = stmt->loadAs<AST::DataDeclLoad>();
        const auto &refs  = decl->refs();
        const auto &inits = stmt->atAs<AST::RepeatedLoad>(1);
        // `let a, b = 1, 2`: one statement, one value for each name.
        if (refs.size() > 1 && refs.size() == inits->size()) {
            for (size_t k = 0; k < refs.size(); ++k) {
                const std::string name = refs[k].toString();
                addSlotCell(name, visitData(inits->atAs<AST::DataLoad>(k)), stmt);
                hoisted.push_back({stmt.get(), i, name});
            }
            continue;
        }
        if (refs.empty() || inits->size() != 1) {
            // No value to take the type from (a native declaration), or a shape the parser does
            // not build: there is no expression the cell could be typed by.
            continue;
        }
        const AST::node_ptr_t init = inits->at(0);
        if (refs.size() == 1) {
            const std::string name = refs[0].toString();
            addSlotCell(name, visitData(init), stmt);
            hoisted.push_back({stmt.get(), i, name});
            continue;
        }
        // `let (a, b) = e`: one value, several names. It is read once, into a cell of its own,
        // and each name is a cell typed by one element of that value. The value is evaluated
        // where the statement stands, and the elements are taken from the cell it was put in,
        // so an arm after a `yield` reads the same value the pull before it read.
        const std::string tupleName = "__gen_tuple_" + std::to_string(idIndex_++);
        addTypedCell(tupleName, visitData(init), stmt);
        cells.push_back({tupleName, nullptr});
        std::vector<std::string> names;
        for (size_t k = 0; k < refs.size(); ++k) {
            node_ptr_t element = tree_->makeAccs(k);
            *element << visitData(init);
            const std::string name = refs[k].toString();
            addSlotCell(name, element, stmt);
            hoisted.push_back({stmt.get(), i, name});
            names.push_back(name);
        }
        destructured.push_back({stmt.get(), tupleName, names});
    }

    // The state one pull hands to the next: the cell the step unpacks (`pc`) and the slots a local's
    // value travels in. It is a value, not a set of cells, and it is the only place the machine's own
    // part is kept from one pull to the next -- the generator body holds no cell for it.
    // The slots are
    // empty today, because a program local of a generator body is a `var T` cell and a cell type has
    // no zero value; the state carries `pc` alone. A local gets a slot in it only when the body
    // builds that local as a value instead of as a cell -- the declaration's own lowering, made
    // while the body is built and not by a later pass (design principles 3.5).
    std::vector<Type *> stateTypes;
    stateTypes.push_back(intLiteral(0)->data()->type());
    for (const CellDecl &cell : cells) {
        if (cell.slotType != nullptr) {
            stateTypes.push_back(cell.slotType);
        }
    }
    Type *stateType = TupleType::create(std::move(stateTypes));
    // The tuple of values a `(…)` of these nodes makes, the way `pairValue` builds one.
    const auto tupleOf = [&](const std::vector<node_ptr_t> &parts) {
        TupleDataFactory factory;
        bool dangling       = false;
        node_ptr_t operands = makeExec(false, true);
        for (const node_ptr_t &part : parts) {
            auto [data, _] = extractData(part, operands, dangling);
            factory.add(data);
        }
        node_ptr_t node = tree_->makeData(factory.build());
        if (dangling) {
            *operands << node;
            node = operands;
        }
        return node;
    };
    // What the next pull starts from: nothing has been written into the slots yet, so each holds the
    // value its type starts at, and `pc` starts at zero.
    const auto stateZero = [&]() {
        std::vector<node_ptr_t> parts = {intLiteral(0)};
        for (const CellDecl &cell : cells) {
            if (cell.slotType != nullptr) {
                parts.push_back(tree_->makeData(zeroValueOf(cell.slotType)));
            }
        }
        return tupleOf(parts);
    };
    // The state as the pull that stands here leaves it: every slot read where the machine writes it,
    // so a `yield` packs the values this pull put there.
    const auto stateNow = [&]() {
        std::vector<node_ptr_t> parts = {readCell(pcCell)};
        for (const CellDecl &cell : cells) {
            if (cell.slotType != nullptr) {
                parts.push_back(readCell(cell.name));
            }
        }
        return tupleOf(parts);
    };
    // What one round hands to the next: the answer the pull has (or has not yet) and whether the
    // round answered at all. Both travel as the loop's *state* -- one value the round produces --
    // and not as cells the round writes. The difference is not cosmetic. The machine's loop tests
    // its flag in a condition of its own, and a cell written inside the round reaches that
    // condition as the value the cell had where the loop stands, which is the value before the
    // first round: with a local cell there, the test is folded to `flag == 0` at build time (the
    // fold in `src/compile/src/gir/builder_variables.cpp`,
    // `collapseNonEscapingVariableReads`) and the round never stops. A state, by contrast, is the
    // one road the loop's condition reads round by round.
    // Measured: a step whose `pc` and whose flag are its own cells hangs every generator case on
    // this tree (the flag is `0 == 0` in the graph); with the flag carried as the round's value the
    // stream plan is back to the tree's own reading.
    const auto roundState = [&](node_ptr_t answer, node_ptr_t answered) {
        // The answer is the state's first part, so it is the answer type the pull hands out and not
        // the pair an arm builds: `(elem, rest)?` and not `(elem, rest)`.
        return tupleOf({declaredValue(answer, answerType, at), answered});
    };
    // The state's own type, so that the loop's first value states it instead of leaving a member
    // unresolved (the answer's part stands for a `cast` node, which has no constant to read).
    Type *const roundStateType = TupleType::create({answerType, intLiteral(0)->data()->type()});
    const auto roundPart = [&](const node_ptr_t &state, size_t index) {
        node_ptr_t part = tree_->makeAccs(index);
        *part << state;
        return part;
    };
    // The arms: one per point of the body. Arm 0 is where a pull starts; the others are reached
    // through `pc`.
    std::vector<node_ptr_t> arms;
    const auto newArm = [&]() {
        arms.push_back(makeExec(synced));
        mark(arms.back(), at, "gct.generator.arm");
        return static_cast<int>(arms.size()) - 1;
    };
    const int first = newArm();
    // A round that sets `pc` and goes on in the same pull.
    const auto jumpTo = [&](int target) {
        node_ptr_t exec = makeExec(synced);
        *exec << writeCell(pcCell, intLiteral(target));
        *exec << readCell(outName);
        mark(exec, at, "gct.generator.jump");
        return exec;
    };
    // The pull is done: the round hands the answer it built (or null at the end of the stream) on
    // with the flag saying the round has answered, so the loop stops.
    const auto stopHere = [&](node_ptr_t &exec) {
        *exec << roundState(roundPart(readCell(outName), 0), intLiteral(1));
    };

    // Fills the arm `pc` with `stmts[from..]`; after them the body goes on at the arm `next`
    // (-1: the stream ends there). `nextState` names the cell a loop body hands the next round
    // (`""`: this stretch is no loop body and hands nothing over).
    std::function<void(int, const AST::node_ptr_t &, size_t, int, const std::string &)> fill =
        [&](int pc,
            const AST::node_ptr_t &stmts,
            size_t from,
            int next,
            const std::string &nextState) {
            node_ptr_t exec = arms[static_cast<size_t>(pc)];
            pushScope();
            // The last value the stretch built: a loop body's last value is the state of the
            // next round. A stretch that ends in a `yield` builds none.
            node_ptr_t last = nullptr;
            // The statement that built it, for the loops that carry several values: their body
            // hands on a tuple, and each element of it is written to its own cell.
            AST::node_ptr_t lastStmt = nullptr;
            for (size_t i = from; i < stmts->size(); ++i) {
                const auto &stmt = stmts->at(i);
                // A cell's name is bound where the stream is created, so the name is in scope
                // for the whole body -- but names of the body resolve in source order, like the
                // names of any function: a read that stands before the statement that writes the
                // cell is a name used before it is declared, and is the same
                // `UnresolvedReference` such a read gets in any function.
                // The check compares positions in the body's own statement list, so it is read
                // only where the stretch walks that list: a block's statements are positions in
                // the block, and a position there says nothing about a declaration of the body.
                if (stmts.get() == bodyStmts.get()) {
                    for (const HoistedCell &cell : hoisted) {
                        if (i > cell.index) {
                            continue;
                        }
                        const AST::node_ptr_t use = nameUseIn(stmt, cell.name);
                        if (use == nullptr) {
                            continue;
                        }
                        diags_->of(SemanticDiag::UnresolvedReference)
                            .at(use->load()->tokenRange())
                            .commit(cell.name);
                        popScope();
                        return;
                    }
                }
                const bool isStmt = stmt->type() == AST::LoadType::Stmt;
                const auto stmtType =
                    isStmt ? stmt->loadAs<AST::StmtLoad>()->stmtType() : AST::StmtType::Expr;
                if (isStmt && stmtType == AST::StmtType::Exit &&
                    stmt->loadAs<AST::ExitStmtLoad>()->exitType() == AST::ExitType::Yield) {
                    // The answer: the element and the stream that continues after this point.
                    const auto &values = stmt->atAs<AST::RepeatedLoad>(0);
                    // Not reachable from source today: the statement parser requires an expression
                    // after `yield`, so a bare `yield` is a syntax error before this point. The
                    // refusal stays because everything below reads `values`; it is a guard on the
                    // shape of the node, not a gate this version chose to keep shut. `yield a, b`
                    // hands out the tuple `(a, b)`, the same element `yield (a, b)` writes. A
                    // `yield` with no value has no element to hand out and keeps the named refusal.
                    if (values->size() == 0) {
                        GUARD(false, "a `yield` with no value reached the generator machine");
                    }
                    const auto elementOf = [&](const auto &list) -> node_ptr_t {
                        if (list->size() == 1) {
                            return visitData(list->front());
                        }
                        TupleDataFactory factory;
                        bool dangling       = false;
                        node_ptr_t execNode = makeExec(false, true);
                        for (size_t k = 0; k < list->size(); ++k) {
                            auto [data, _] =
                                extractData(visitData(list->at(k)), execNode, dangling);
                            factory.add(data);
                        }
                        node_ptr_t tupleNode = tree_->makeData(factory.build());
                        if (dangling) {
                            *execNode << tupleNode;
                            tupleNode = execNode;
                        }
                        return tupleNode;
                    };
                    const int resume = newArm();
                    fill(resume, stmts, i + 1, next, nextState);
                    *exec << writeCell(pcCell, intLiteral(resume));
                    node_ptr_t elem = declaredValue(elementOf(values), elemType, stmt);
                    // The value of the round is the answer of the pull: a `return` is written
                    // only in the step function's own body
                    // (docs/design/io-concurrency-streams/plan.cn.md, slice E), so the element
                    // and the rest of the stream are the arm's value, and the round answers.
                    *exec << roundState(pairValue(elem, streamValue(stateNow())), intLiteral(1));
                    popScope();
                    return;
                }
                if (isStmt && stmtType == AST::StmtType::Exit) {
                    // `return`: the stream ends, without a value of its own.
                    stopHere(exec);
                    popScope();
                    return;
                }
                if (isStmt && stmtType == AST::StmtType::Data) {
                    // A declaration of the body that became a cell: the cell is made where the
                    // stream is created, and its value is written here, where the statement
                    // stands -- by the arm that reaches it, in source order.
                    const auto destr = std::find_if(
                        destructured.begin(),
                        destructured.end(),
                        [&](const Destructured &one) { return one.decl == stmt.get(); });
                    if (destr != destructured.end()) {
                        // The value once, into its own cell, then one element per name.
                        const auto &inits = stmt->atAs<AST::RepeatedLoad>(1);
                        *exec << writeCell(destr->tupleName, visitData(inits->at(0)));
                        for (size_t k = 0; k < destr->names.size(); ++k) {
                            node_ptr_t element = tree_->makeAccs(k);
                            *element << readCell(destr->tupleName);
                            *exec << writeCell(destr->names[k], element);
                        }
                        continue;
                    }
                    if (std::any_of(hoisted.begin(), hoisted.end(), [&](const HoistedCell &cell) {
                            return cell.decl == stmt.get();
                        })) {
                        const auto &decl  = stmt->loadAs<AST::DataDeclLoad>();
                        const auto &inits = stmt->atAs<AST::RepeatedLoad>(1);
                        for (size_t k = 0; k < decl->refs().size(); ++k) {
                            *exec << writeCell(
                                decl->refs()[k].toString(),
                                visitData(inits->atAs<AST::DataLoad>(k < inits->size() ? k : 0)));
                        }
                        continue;
                    }
                    // One that did not: its value depends on this point, so it is built here and
                    // is this stretch's own. Only a declaration with no value at all comes this
                    // far (a native one), and the arms have no cell to reach for it.
                }
                if (writesOwnYield(stmt)) {
                    // A branch, a loop, or a `match` with a `yield` in it: each point of those
                    // has to be an arm of its own.
                    const bool isIf =
                        isStmt && stmtType == AST::StmtType::Expr && stmt->size() > 0 &&
                        stmt->at(0)->type() == AST::LoadType::Data &&
                        stmt->at(0)->loadAs<AST::DataLoad>()->dataType() == AST::DataType::IfExpr;
                    const bool isMatch = isStmt && stmtType == AST::StmtType::Expr &&
                                         stmt->size() > 0 &&
                                         stmt->at(0)->type() == AST::LoadType::Data &&
                                         stmt->at(0)->loadAs<AST::DataLoad>()->dataType() ==
                                             AST::DataType::MatchExpr;
                    const bool isLoop =
                        isStmt && stmtType == AST::StmtType::Expr && stmt->size() > 0 &&
                        stmt->at(0)->type() == AST::LoadType::Data &&
                        stmt->at(0)->loadAs<AST::DataLoad>()->dataType() == AST::DataType::LoopExpr;
                    if (!isIf && !isMatch && !isLoop) {
                        if (isStmt && stmtType == AST::StmtType::Block) {
                            // A plain block is a grouping, not a point of its own: its statements
                            // are this stretch's statements, and the pull goes on at the arm after
                            // the block. One more arm carries them.
                            const int blockPc = newArm();
                            const int afterPc = newArm();
                            fill(afterPc, stmts, i + 1, next, nextState);
                            fill(blockPc, stmt->atAs<AST::RepeatedLoad>(0), 0, afterPc, nextState);
                            *exec << jumpTo(blockPc);
                            popScope();
                            return;
                        }
                        GUARD(
                            false,
                            "a statement the generator body does not build reached the machine");
                    }
                    if (isLoop) {
                        // A `while` loop whose body writes a `yield`. The pull that hands an
                        // element out stops in the arm the `yield` left, so the loop can neither
                        // be restarted from the top nor be left as one stretch: what the loop
                        // carries is one cell, its body is an arm of its own, and the arm after
                        // the body goes back to the condition.
                        const auto &loopAst        = stmt->at(0);
                        const auto &loopLoad       = loopAst->loadAs<AST::LoopExprLoad>();
                        const auto &inits          = loopAst->atAs<AST::RepeatedLoad>(0);
                        const AST::node_ptr_t init = inits->size() == 1 ? inits->at(0) : nullptr;
                        if (loopLoad->hasFor()) {
                            // `loop s = 0 for x in xs do ...`: the machine carries the sequence
                            // once, where the loop stands, a position that moves by one each round,
                            // and the fold state the body hands on. Each round writes the element
                            // at that position into the binder's cell before the body runs.
                            const auto &seqAst = loopAst->optAtAs<AST::DataLoad>(1);
                            if (seqAst == nullptr ||
                                loopAst->optAtAs<AST::DataLoad>(2) != nullptr ||
                                loopLoad->state().size() != 1 || init == nullptr ||
                                !hoistable(init)) {
                                GUARD(
                                    false,
                                    "a `loop ... for` clause reached the generator machine");
                            }
                            const std::string stateName = loopLoad->state()[0].toString();
                            if (!isCell(stateName)) {
                                addTypedCell(stateName, visitData(init), stmt);
                                cells.push_back({stateName, nullptr});
                            }
                            const std::string seqName =
                                "__gen_for_seq_" + std::to_string(idIndex_++);
                            node_ptr_t sequence = visitData(seqAst);
                            addCellOfType(seqName, typeOfPlaceholder(sequence), stmt);
                            cells.push_back({seqName, nullptr});
                            const std::string idxName =
                                "__gen_for_idx_" + std::to_string(idIndex_++);
                            addTypedCell(idxName, intLiteral(0), stmt);
                            cells.push_back({idxName, nullptr});
                            const auto elementAt = [&]() {
                                node_ptr_t node = tree_->makeLink(2);
                                *node << tree_->makeDRef("__idx__") << readCell(seqName)
                                      << readCell(idxName);
                                return mark(node, stmt, "gct.generator.for.element");
                            };
                            const std::string binderName = loopLoad->binder().toString();
                            if (!isCell(binderName)) {
                                // The binder's type is the type of the element the loop reads: the
                                // same expression the loop writes into the cell, asked with
                                // `typeof` (design principles 5.6: `typeof` gives the type a value
                                // would have and evaluates nothing), so the position is static.
                                // What an element of a container is, is what indexing it gives --
                                // the same rule the library's `ElementOf` writes in Camel -- so the
                                // comprehension needs no type function of its own.
                                addTypedCell(binderName, elementAt(), stmt);
                                cells.push_back({binderName, nullptr});
                            }
                            const int afterPc = newArm();
                            fill(afterPc, stmts, i + 1, next, nextState);
                            const int condPc = newArm();
                            const int bodyPc = newArm();
                            const int stepPc = newArm();
                            {
                                node_ptr_t length = tree_->makeLink(1);
                                *length << tree_->makeDRef("len") << readCell(seqName);
                                node_ptr_t condition = tree_->makeLink(2);
                                *condition << tree_->makeDRef("__lt__") << readCell(idxName)
                                           << length;
                                node_ptr_t brch = tree_->makeBrch();
                                *brch << condition;
                                node_ptr_t trueCase = tree_->makeCase(CaseType::True);
                                *trueCase << jumpTo(bodyPc);
                                node_ptr_t elseCase = tree_->makeCase(CaseType::Else);
                                *elseCase << jumpTo(afterPc);
                                *brch << trueCase << elseCase;
                                mark(brch, stmt, "gct.generator.branch");
                                *arms[static_cast<size_t>(condPc)] << brch;
                            }
                            *arms[static_cast<size_t>(bodyPc)]
                                << writeCell(binderName, elementAt());
                            fill(
                                bodyPc,
                                loopAst->atAs<AST::StmtBlockLoad>(3)->atAs<AST::RepeatedLoad>(0),
                                0,
                                stepPc,
                                stateName);
                            {
                                node_ptr_t nextIndex = tree_->makeLink(2);
                                *nextIndex << tree_->makeDRef("__add__") << readCell(idxName)
                                           << intLiteral(1);
                                *arms[static_cast<size_t>(stepPc)] << writeCell(idxName, nextIndex);
                                *arms[static_cast<size_t>(stepPc)] << jumpTo(condPc);
                            }
                            *exec << writeCell(seqName, visitData(seqAst));
                            *exec << writeCell(idxName, intLiteral(0));
                            *exec << writeCell(stateName, visitData(init));
                            *exec << jumpTo(condPc);
                            popScope();
                            return;
                        }
                        // What the machine carries is one value or several, and each is read
                        // once, where the loop stands: a `for` has a sequence and a binder the
                        // arms do not run. A first value that is a constant or a lambda is the
                        // one the loop would start from in any arm; one that depends on the point
                        // the loop stands at is not lowered yet (a cell whose type is inferred is
                        // in place, but reading the cells a computed first value names across
                        // the arm boundary is the next step).
                        if (inits->size() == 0 || loopLoad->state().empty() || loopLoad->hasFor() ||
                            inits->size() != loopLoad->state().size()) {
                            GUARD(
                                false,
                                "a `loop` with no state, or a state with no first value, reached "
                                "the machine");
                        }
                        // One cell per carried name. The body already may have a cell of that name
                        // (a `var` of its own): the loop carries that one cell, and the value below
                        // is written into it where the loop stands. A second cell of the name
                        // would be a second writing of one variable; the name is one cell with one
                        // type.
                        std::vector<std::string> stateNames;
                        for (size_t k = 0; k < loopLoad->state().size(); ++k) {
                            const std::string name         = loopLoad->state()[k].toString();
                            const AST::node_ptr_t eachInit = inits->at(k);
                            // The initial value is evaluated where the loop stands (see the write
                            // below), once, before the first condition: a value computed there, or
                            // read from a local, is the first round's state like a constant is.
                            if (!isCell(name)) {
                                addTypedCell(name, visitData(eachInit), stmt);
                                cells.push_back({name, nullptr});
                            }
                            stateNames.push_back(name);
                        }
                        // The cells the next round starts from: one name, or a list for a loop
                        // that carries several, whose body's last value is a tuple of them.
                        std::string stateName;
                        for (size_t k = 0; k < stateNames.size(); ++k) {
                            if (k != 0) {
                                stateName += ",";
                            }
                            stateName += stateNames[k];
                        }
                        // The four points of the loop: past it, at the condition, in the body,
                        // and the step back to the condition.
                        const int afterPc = newArm();
                        fill(afterPc, stmts, i + 1, next, nextState);
                        const int condPc    = newArm();
                        const int bodyPc    = newArm();
                        const int stepPc    = newArm();
                        const auto &condAst = loopAst->optAtAs<AST::DataLoad>(2);
                        if (condAst) {
                            node_ptr_t brch = tree_->makeBrch();
                            *brch << visitData(condAst);
                            node_ptr_t trueCase = tree_->makeCase(CaseType::True);
                            *trueCase << jumpTo(bodyPc);
                            node_ptr_t elseCase = tree_->makeCase(CaseType::Else);
                            *elseCase << jumpTo(afterPc);
                            *brch << trueCase << elseCase;
                            mark(brch, stmt, "gct.generator.branch");
                            *arms[static_cast<size_t>(condPc)] << brch;
                        } else {
                            // No condition: the body is entered and the loop ends only where
                            // the body does.
                            *arms[static_cast<size_t>(condPc)] << jumpTo(bodyPc);
                        }
                        // The body: its last value is the state of the next round, so it is
                        // written into the cell before the round goes on at the step arm.
                        fill(
                            bodyPc,
                            loopAst->atAs<AST::StmtBlockLoad>(3)->atAs<AST::RepeatedLoad>(0),
                            0,
                            stepPc,
                            stateName);
                        *arms[static_cast<size_t>(stepPc)] << jumpTo(condPc);
                        // The pull that reaches the loop enters it after writing the state the
                        // first round starts from: the initial value is evaluated here, where the
                        // loop stands.
                        for (size_t k = 0; k < stateNames.size(); ++k) {
                            *exec << writeCell(stateNames[k], visitData(inits->at(k)));
                        }
                        *exec << jumpTo(condPc);
                        popScope();
                        return;
                    }
                    if (isMatch) {
                        // The subject is read at this point, and the arm the value picks becomes
                        // the point the next round of the same pull runs.
                        const auto &matchAst = stmt->at(0);
                        const int afterMatch = newArm();
                        fill(afterMatch, stmts, i + 1, next, nextState);
                        // The matched value outlives the `yield`s the arms are cut at, so it is put
                        // in a cell where the `match` stands and the branch reads the cell. A cell
                        // declared in the walk is captured with the others (the capture loop walks
                        // every cell this body declared, which the walk filled), so an arm that
                        // starts after a `yield` still finds it written.
                        node_ptr_t subject = visitData(matchAst->atAs<AST::DataLoad>(0));
                        const std::string subjectCell =
                            "__gen_match_subject_" + std::to_string(cells.size());
                        addTypedCell(subjectCell, subject, stmt);
                        cells.push_back({subjectCell, nullptr});
                        *exec << writeCell(subjectCell, subject);
                        node_ptr_t brch = tree_->makeBrch();
                        *brch << readCell(subjectCell);
                        for (const auto &caseNode : *matchAst->atAs<AST::RepeatedLoad>(1)) {
                            const auto &pattern       = caseNode->at(0);
                            const bool writtenPattern = pattern->type() == AST::LoadType::Data;
                            const auto patternKind =
                                writtenPattern ? pattern->loadAs<AST::DataLoad>()->dataType()
                                               : AST::DataType::Literal;
                            const bool isEnumPattern =
                                writtenPattern && patternKind == AST::DataType::EnumPattern;
                            const bool isRefPattern =
                                writtenPattern && patternKind == AST::DataType::Ref;
                            // The names a case binds are locals of the body like any other, and a
                            // local that lives across a `yield` is part of the state (design
                            // principles 5.4). So each is a cell of the body, registered with the
                            // declared locals and the loop binders -- not with a state of its own.
                            // The case binds a name of its own, because a cell's name is what every
                            // arm reads; the body the case runs writes the cell where the value is
                            // in hand, and the arm and the pull after a `yield` in it then read the
                            // same cell.
                            std::vector<std::string> patternFields;
                            std::vector<std::string> patternBindings;
                            // The cell each bound name is written into, and the name the case
                            // binds it to.
                            std::vector<std::pair<std::string, std::string>> cellWrites;
                            if (isEnumPattern) {
                                const auto enumPattern = pattern->loadAs<AST::EnumPatternLoad>();
                                // The pattern names the enum, so that name resolves here the way a
                                // type annotation written at this point would, and the variant it
                                // names gives the type of each field.
                                const EnumVariant *variant = nullptr;
                                if (!enumPattern->ref().paths().empty()) {
                                    const Reference enumName(enumPattern->ref().paths());
                                    if (Type *declared = lookupType(enumName, pattern);
                                        declared != nullptr) {
                                        if (EnumType *enumType = asEnumType(declared)) {
                                            const auto tag = enumType->decl()->findVariant(
                                                enumPattern->ref().ident());
                                            if (tag.has_value()) {
                                                variant = &enumType->decl()->variants()[*tag];
                                            }
                                        }
                                    }
                                }
                                for (size_t k = 0; k < enumPattern->fields().size(); ++k) {
                                    const AST::EnumPatternField &field = enumPattern->fields()[k];
                                    patternFields.push_back(field.name);
                                    if (field.binding.empty()) {
                                        patternBindings.push_back(std::string());
                                        continue;
                                    }
                                    const std::string holder =
                                        "__gen_match_bind_" + std::to_string(idIndex_++);
                                    patternBindings.push_back(holder);
                                    Type *fieldType = nullptr;
                                    if (variant != nullptr) {
                                        const auto at = enumPattern->structShape()
                                                            ? variant->findField(field.name)
                                                            : std::optional<size_t>(k);
                                        if (at.has_value() && *at < variant->fieldCount()) {
                                            fieldType = variant->fieldTypes[*at];
                                        }
                                    }
                                    if (fieldType != nullptr) {
                                        cellWrites.emplace_back(field.binding, holder);
                                        if (!isCell(field.binding)) {
                                            addCellOfType(field.binding, fieldType, caseNode);
                                            cells.push_back({field.binding, nullptr});
                                        }
                                    }
                                }
                            } else if (isRefPattern) {
                                // `case v` binds the payload of a `T?` / `T!`; `case error e` binds
                                // the error of a `T!`, and that one is not the payload. Every
                                // fallible value carries the one `Error` structure (design
                                // principles 3.9), and the prelude declares that type under the
                                // name `Error`, so the cell a `case error e` binds is typed by
                                // reading the declaration, the way a written annotation would.
                                const bool isError     = pattern->size() > 0;
                                const std::string name = (isError ? pattern->front() : pattern)
                                                             ->loadAs<AST::RefDataLoad>()
                                                             ->ref()
                                                             .toString();
                                const std::string holder =
                                    "__gen_match_bind_" + std::to_string(idIndex_++);
                                patternBindings.push_back(holder);
                                cellWrites.emplace_back(name, holder);
                                if (!isCell(name)) {
                                    Type *cellType = nullptr;
                                    if (isError) {
                                        // The error of a `T!` is not the payload the tagged value
                                        // holds, so `typeof` the value does not give it. What does
                                        // give it is the one `Error` structure every fallible
                                        // value carries (design principles 3.9) -- the same object
                                        // the graph builder compares a failing value against, so
                                        // the cell is typed by the language's own fact and not by
                                        // one the builder picks.
                                        cellType = errorType();
                                    } else {
                                        // `PayloadOf<T?>` is the `U` of `U?`, and of `U!` too.
                                        std::vector<Type *> payloadArgument = {
                                            typeOfPlaceholder(readCell(subjectCell))};
                                        cellType =
                                            TypeCallType::create("PayloadOf", payloadArgument);
                                    }
                                    addCellOfType(name, cellType, caseNode);
                                    cells.push_back({name, nullptr});
                                }
                            }
                            const int armPc = newArm();
                            // What the case runs: the cells its bindings live in are written
                            // where the value is in hand, then the arm goes on at its own point.
                            node_ptr_t entry = makeExec(synced);
                            for (const auto &[cellName, holder] : cellWrites) {
                                *entry << writeCell(cellName, tree_->makeDRef(holder));
                            }
                            *entry << jumpTo(armPc);
                            mark(entry, caseNode, "gct.generator.jump");
                            fill(
                                armPc,
                                caseNode->atAs<AST::StmtBlockLoad>(1)->atAs<AST::RepeatedLoad>(0),
                                0,
                                afterMatch,
                                nextState);
                            if (pattern->type() == AST::LoadType::Null) {
                                // `case _`: the arm every other value takes.
                                node_ptr_t gCase = tree_->makeCase(CaseType::Else);
                                *gCase << entry;
                                *brch << gCase;
                                continue;
                            }
                            if (isEnumPattern) {
                                const auto enumPattern = pattern->loadAs<AST::EnumPatternLoad>();
                                node_ptr_t gCase       = tree_->makeEnumCase(
                                    enumPattern->ref().toString(),
                                    patternFields,
                                    patternBindings,
                                    enumPattern->structShape());
                                *gCase << entry;
                                *brch << gCase;
                                continue;
                            }
                            if (isRefPattern) {
                                // `case v` takes the value side of a `T?` / `T!`; `case error e`
                                // takes the side that failed (design principles 3.8).
                                node_ptr_t gCase = tree_->makeCase(
                                    pattern->size() > 0 ? CaseType::Error : CaseType::Bind,
                                    patternBindings[0]);
                                *gCase << entry;
                                *brch << gCase;
                                continue;
                            }
                            node_ptr_t gCase = tree_->makeCase(CaseType::Value);
                            *gCase << visitData(caseNode->atAs<AST::DataLoad>(0)) << entry;
                            *brch << gCase;
                        }
                        mark(brch, stmt, "gct.generator.match");
                        *exec << brch;
                        popScope();
                        return;
                    }
                    const auto &ifAst = stmt->at(0);
                    const int after   = newArm();
                    fill(after, stmts, i + 1, next, nextState);
                    const int thenPc = newArm();
                    fill(
                        thenPc,
                        ifAst->atAs<AST::StmtBlockLoad>(1)->atAs<AST::RepeatedLoad>(0),
                        0,
                        after,
                        nextState);
                    const int elsePc      = newArm();
                    const auto &elseBlock = ifAst->optAtAs<AST::StmtBlockLoad>(2);
                    if (elseBlock) {
                        fill(elsePc, elseBlock->atAs<AST::RepeatedLoad>(0), 0, after, nextState);
                    } else {
                        *arms[static_cast<size_t>(elsePc)] << jumpTo(after);
                    }
                    node_ptr_t brch = tree_->makeBrch();
                    *brch << visitData(ifAst->atAs<AST::DataLoad>(0));
                    node_ptr_t trueCase = tree_->makeCase(CaseType::True);
                    *trueCase << jumpTo(thenPc);
                    node_ptr_t elseCase = tree_->makeCase(CaseType::Else);
                    *elseCase << jumpTo(elsePc);
                    *brch << trueCase << elseCase;
                    mark(brch, stmt, "gct.generator.branch");
                    *exec << brch;
                    popScope();
                    return;
                }
                // The value the statement built. A stretch that hands a value to the next round
                // (`nextState`) keeps the last one for that cell instead of running it in the
                // arm's own chain: a node has one parent, so the write is the only place it
                // stands, and the write is the arm's last step.
                node_ptr_t value = visitStmt(stmt);
                if (!nextState.empty() && i + 1 == stmts->size()) {
                    last     = value;
                    lastStmt = stmt;
                } else {
                    *exec << value;
                }
            }
            if (!nextState.empty()) {
                // A loop body hands the next round the last value it built. A stretch that
                // builds none (one that ends in a `yield`, or whose last statement is a `var`
                // already written as a cell) would go round without moving: refused by name
                // here rather than run as a loop that never advances.
                if (last == nullptr) {
                    reportLoopBodyShape(from < stmts->size() ? stmts->at(from) : stmts);
                }
                // One name takes the value itself; a list of names takes the tuple the body built,
                // one element per name -- the shape a loop that carries several values hands on:
                // `(i + 1, s + i)` for `loop i = 0, s = 0 ...`. The elements are built where the
                // writes stand, one visit each.
                if (nextState.find(',') == std::string::npos) {
                    *exec << writeCell(nextState, last);
                } else {
                    std::vector<std::string> names;
                    size_t at  = 0;
                    size_t end = 0;
                    while (at <= nextState.size()) {
                        end = nextState.find(',', at);
                        names.push_back(nextState.substr(
                            at,
                            end == std::string::npos ? std::string::npos : end - at));
                        if (end == std::string::npos) {
                            break;
                        }
                        at = end + 1;
                    }
                    const AST::node_ptr_t tupleAst =
                        lastStmt != nullptr && lastStmt->type() == AST::LoadType::Stmt
                            ? lastStmt->at(0)
                            : nullptr;
                    const AST::node_ptr_t tupleItems =
                        tupleAst != nullptr && tupleAst->type() == AST::LoadType::Data &&
                                tupleAst->loadAs<AST::DataLoad>()->dataType() ==
                                    AST::DataType::Tuple &&
                                tupleAst->size() == 1
                            ? tupleAst->at(0)
                            : nullptr;
                    if (tupleItems == nullptr || tupleItems->size() != names.size()) {
                        // A loop that carries several values hands them on as a tuple of the
                        // same length; anything else has no value for one of the names.
                        reportLoopBodyShape(lastStmt != nullptr ? lastStmt : stmts->at(from));
                    }
                    // The next values are built before any of them is written: a state name the
                    // second element reads must be the value of this round, not the one the first
                    // write already put there.
                    std::vector<std::string> nextCells;
                    for (size_t k = 0; k < names.size(); ++k) {
                        const std::string nextName = "__gen_next_" + std::to_string(idIndex_++);
                        addTypedCell(nextName, readCell(names[k]), lastStmt);
                        cells.push_back({nextName, nullptr});
                        nextCells.push_back(nextName);
                    }
                    for (size_t k = 0; k < names.size(); ++k) {
                        *exec << writeCell(nextCells[k], visitData(tupleItems->at(k)));
                    }
                    for (size_t k = 0; k < names.size(); ++k) {
                        *exec << writeCell(names[k], readCell(nextCells[k]));
                    }
                }
            }
            if (next >= 0) {
                *exec << writeCell(pcCell, intLiteral(next));
                *exec << readCell(outName);
            } else {
                stopHere(exec);
            }
            popScope();
        };
    // `step`: one round of the loop runs the arm `pc` names, until one of them answers. The
    // state arrives as its argument, and every slot it carries becomes the step's own local here,
    // *before* any arm is built: a name resolves where it is written, so a local declared after the
    // arms would leave them reading the enclosing body's cells.
    node_ptr_t stepBody = makeExec(synced);
    const auto unpackSlot = [&](const std::string &name, size_t index) {
        // One reference per accessor: this tree gives a node one parent, so one `DRef` cannot hang
        // under two `:accs` nodes.
        node_ptr_t accs = tree_->makeAccs(index);
        *accs << tree_->makeDRef(stateParamName);
        node_ptr_t vari = tree_->makeVari();
        node_ptr_t ref  = tree_->makeNRef(name);
        *ref << accs;
        *vari << ref;
        mark(vari, at, "gct.generator.state");
        *stepBody << vari;
    };
    unpackSlot(pcCell, 0);
    size_t slotPart = 1;
    for (const CellDecl &cell : cells) {
        if (cell.slotType != nullptr) {
            unpackSlot(cell.name, slotPart++);
        }
    }
    fill(first, bodyStmts, 0, -1, std::string());
    // Every cell that is still a cell is read here, before any arm names `step` again: a closure
    // holds the captures its function had when it was made, so a capture made later could not be
    // passed on. A cell with a slot is the step's own local -- every arm reads it there, and the
    // state carries its value -- so it is not one of these. Which of the two a cell is was decided
    // where it was declared (`CellDecl`), not here.
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].slotType != nullptr) {
            continue;
        }
        node_ptr_t touch = tree_->makeNRef("__gen_touch__" + std::to_string(i));
        *touch << readCell(cells[i].name);
        *stepBody << touch;
    }
    node_ptr_t loopNode = tree_->makeLoop(camel::core::LoopKind::While, {outName}, std::string());
    mark(loopNode, at, "gct.generator.loop");
    // The state the loop starts from: no answer yet, and the round has not answered.
    *loopNode << declaredValue(roundState(tree_->makeData(Data::null()), intLiteral(0)),
                               roundStateType,
                               at);
    node_ptr_t condition = makeExec();
    // The round goes on while the state it was handed says no arm has answered. The flag is read
    // from the state, which is the round's own value: reading it from a cell would test the value
    // the cell had where the loop stands, round after round.
    node_ptr_t answered = tree_->makeLink(2);
    *answered << tree_->makeDRef("__eq__") << roundPart(readCell(outName), 1) << intLiteral(0);
    *condition << answered;
    *loopNode << condition;
    node_ptr_t round = makeExec(synced);
    node_ptr_t chain = nullptr;
    for (size_t pc = arms.size(); pc-- > 0;) {
        node_ptr_t brch = tree_->makeBrch();
        *brch << equalTo(pcCell, static_cast<int64_t>(pc));
        node_ptr_t trueCase = tree_->makeCase(CaseType::True);
        *trueCase << arms[pc];
        node_ptr_t elseCase = tree_->makeCase(CaseType::Else);
        if (chain == nullptr) {
            node_ptr_t end = makeExec(synced);
            stopHere(end);
            *elseCase << end;
        } else {
            node_ptr_t wrapper = makeExec(synced);
            *wrapper << chain;
            *elseCase << wrapper;
        }
        *brch << trueCase << elseCase;
        mark(brch, at, "gct.generator.select");
        chain = brch;
    }
    *round << chain;
    *loopNode << round;
    // What the step answers: the state's first part, which the last round wrote -- the answer a
    // `yield` built, or null where the stream ended.
    *stepBody << roundPart(loopNode, 0);
    node_ptr_t stepFunc = tree_->makeFunc(stepName);
    mark(stepFunc, funcAst, "gct.generator.step");
    // The step takes the state as a normal parameter: a call's positional argument is matched
    // against `normTypes`, never `withTypes` (which is `with <..ws>`). It is named, so the port
    // `src/compile/gir/builder_helpers.h` makes carries that name; an unnamed parameter would be
    // port `__w0` with no name in scope.
    FunctionTypeFactory stepSignature;
    stepSignature.addNormArg(stateParamName, stateType, false);
    stepSignature.setExitType(stepType->exitType());
    FunctionType *stepTypeWithState = stepSignature.build();
    *stepFunc << tree_->makeType(stepTypeWithState) << stepBody;
    *outer << stepFunc;
    node_ptr_t ret = tree_->makeExit(ExitType::Return);
    *ret << streamValue(stateZero());
    mark(ret, funcAst, "gct.generator.return");
    *outer << ret;
    LEAVE("GeneratorBody");
    return outer;
}

} // namespace camel::compile::gct