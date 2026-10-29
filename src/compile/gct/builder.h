// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "camel/common/scope.h"
#include "camel/compile/gct.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/module.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/type/enum.h"
#include "camel/parse/ast.h"

namespace camel::compile::gct {

using FunctionType = camel::core::type::FunctionType;
using void_ptr_t   = void *;
// The resource model a function type publishes (design effects plan 2.2).
namespace sem = camel::core::sem;

class Builder {
  public:
    Builder(
        const camel::core::context::context_ptr_t &context,
        const camel::core::module::module_ptr_t &module)
        : context_(context), module_(module) {
        typeScope_ = std::make_shared<Scope<Reference, Type *>>();
    };
    virtual ~Builder() = default;

    /// The tree of the module whose AST is `node`. Null when the module could not be lowered
    /// at all; errors that leave a tree behind are in `diags`.
    std::unique_ptr<Tree> build(AST::node_ptr_t node, camel::core::error::diagnostics_ptr_t diags) {
        idIndex_ = 0;
        pendingNames_.clear();
        diags_   = diags;
        tree_    = std::make_unique<Tree>();
        initInnerTypes();
        try {
            tree_->setRoot(visitModule(node));
        } catch (camel::core::error::Diagnostic &d) {
            diags_->add(std::move(d));
            tree_.reset();
        }
        return std::move(tree_);
    }

  private:
    // The tree being built: every node of the module is made by it.
    std::unique_ptr<Tree> tree_;
    size_t idIndex_ = 0;
    scope_ptr_t<Reference, Type *> typeScope_;
    // Type names whose declaration (or whose import) failed: a use of one is a consequence of
    // that error and is not reported as an unresolved type of its own. When an import of a
    // whole module failed, which names it would have brought is unknown, and no unresolved
    // type is reported at all.
    std::unordered_set<std::string> poisonedTypes_;
    bool wholeImportFailed_ = false;
    // Namespaces (design principles 10.2). The namespace path of the top-level declaration
    // being lowered (`A::B` for `A::B::f`): a type name written in it is looked up there, then
    // in the enclosing namespaces, then at the top level. The `using namespace` in force, one
    // frame per block (the module's, a namespace block's, a block of a function body). The
    // namespaces the module declares (prefixes of its qualified names).
    std::vector<std::string> namespacePath_;
    std::vector<std::vector<Reference>> usingFrames_;
    std::unordered_set<std::string> ownNamespaces_;
    // The type a name written in a type position stands for, by the one rule of precedence:
    // the module's types (from the innermost namespace out; the module's own name may qualify
    // them), then imported by name, then through a `using namespace`, then built in. Reports
    // and throws when there is none, or when two `using` give different types (AmbiguousName).
    Type *lookupType(const Reference &ref, const AST::node_ptr_t &ast);
    /// `ref`, written at `ast`, names the type `remote` of the module `source`, which is
    /// not declared yet. When that declaration is in progress further up, the types form a
    /// cycle across modules: reported with the cycle (ModuleTypeCycle; design principles 10.1).
    /// When it is on a cycle reported already, the failure follows from that report and is not
    /// reported again. Either way BuildAbortException; otherwise nothing happens.
    void rejectTypeCycle(
        const std::shared_ptr<camel::core::module::Module> &source, const Reference &remote,
        const Reference &ref, const AST::node_ptr_t &ast);
    // Whether `ref` names a namespace a `using namespace` can open: one of the module (also
    // relative to the namespace being lowered) or one bound by an import.
    bool usableNamespace(const Reference &ref) const;
    // The names the module exports (visitModule), and the modules its imports bound, with the
    // import that bound each (compileImportedModules).
    std::unordered_set<std::string> exportedRefs_;
    // The DECL node of the type declaration whose right-hand side is being lowered: the tags
    // written on the fields of the struct it writes belong to that declaration (visitTypeDecl,
    // visitStructType). Null outside a type declaration.
    node_ptr_t fieldTagOwner_ = nullptr;
    std::vector<std::pair<AST::node_ptr_t, camel::core::module::module_ptr_t>> importedModules_;
    // Compiles the imported modules written in Camel as far as their declarations; one in
    // error is reported at its import (ImportedModuleHasErrors).
    void compileImportedModules();
    std::unordered_map<void *, FunctionType *> funcDecls_;

    camel::core::context::context_ptr_t context_;
    camel::core::module::module_ptr_t module_;
    camel::core::error::diagnostics_ptr_t diags_;

    void initInnerTypes();

    std::pair<node_ptr_t, data_ptr_t> makeRefData(const node_ptr_t &expr);
    // The elements of a literal that are plain names and have not been hoisted: they are read
    // where the literal is built, which is after every element hoisted before it. When a computed
    // element is hoisted after them, they are hoisted first (`pendingNames_`), so the uses of one
    // variable in a literal happen from left to right, like the arguments of a call. Keyed by the
    // block the literal hoists into, which the entry keeps alive.
    struct PendingName {
        std::shared_ptr<camel::core::data::RefData> data;
        node_ptr_t expr;
    };
    std::vector<std::pair<node_ptr_t, std::vector<PendingName>>> pendingNames_;
    std::pair<data_ptr_t, bool> extractData(const node_ptr_t &node, node_ptr_t &execNode);
    std::pair<data_ptr_t, bool>
    extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling);

    // Type variables and type functions (design principles 5.6; camel/core/type/typevar.h).
    // A generic declaration declares its type parameters at its head (`func first::<T>(...)`);
    // the builder puts them in scope before it visits a parameter type, so one of them wins over
    // a type of the same name (design principles 5.5), and records them in declaredTypeVars_ so
    // the signature can be checked. A type function may only appear as the base of `Name<...>`
    // (typeFunctionBase_).
    node_ptr_t makeExec(bool synced = false, bool operands = false);

    bool funcDeclPending_ = false;
    bool declSignature_   = false;
    // The function type visited next is the signature of a function being defined (a
    // FuncData): it is built open (camel/core/type/composite/func.h), so that the graph
    // builder can set the result type the body decides when none is written. Every other
    // function type is interned.
    bool openSignature_    = false;
    bool typeFunctionBase_ = false;
    std::vector<std::string> declaredTypeVars_;
    // The head parameters of the declaration being visited that were written with `..`: they are
    // packs, not one type each (`func zip::<..A>`, design principles 5.6). A pack has no
    // occurrence of its own in the signature: it is written only in a `..` expansion, and GCT
    // works the name of that expansion out from the one pack variable its body names.
    std::vector<std::string> declaredPackVars_;

    // The head names written somewhere in the signature being visited. A name declared at the
    // head but written nowhere is 'unused'; one written only where type construction drops it
    // (the argument of a type function that does not use it) is 'not bindable'.
    std::unordered_set<std::string> writtenHeadVars_;
    bool signatureVisit_ = false;

    // The type variables the declaration being visited declares at its head, and the occurrence
    // in scope for each (inserted into the signature's type scope). A use of one in the signature
    // is a binding occurrence: any argument position of that type can fill it, and the first one
    // that does fixes it (camel/core/type/typevar.h, bindTypeVars). The map is per declaration so
    // that a name of an enclosing declaration stays a plain use.
    std::shared_ptr<std::unordered_map<std::string, Type *>> headTypeVarTypes_;

    // The lambda parameters whose type was left out, collected while the signature of the
    // function being defined is visited and given to its FUNC node (Tree::setUnwrittenParams).
    // Only the signature of a FuncData (openSignature_) may leave one out; the flag is taken
    // by visitFuncData right after the signature.
    std::vector<Tree::UnwrittenParam> pendingUnwrittenParams_;
    // Head type parameters the signature never wrote, held until the body is built: a body that
    // uses the name (`match type T`) makes it a used parameter, not an unused one.
    std::vector<std::string> pendingUnusedHeadVars_;

    // Named dimensions of type extents (design principles 5.4): a name denotes an extent, not a
    // type. A name is bound the first time it is written in the parameter types of the function
    // declaration being visited; later occurrences in the same signature use it, and the return
    // type and the body may use the names the parameters bound. `dimNames_` holds the names the
    // signature binds (empty outside a signature, and in the body of a nested declaration);
    // `boundDims_` holds the names already visited, so a second plain name binds as a reference,
    // not a new dimension. `dimUses_` counts the occurrences in the parameter types so a name
    // used only once can be suggested as `?`. `dimBindAllowed_` is true only while the parameter
    // types are visited, so a name in a return type or in a body that no parameter bound, or one
    // inside a nested function type, is reported as unbound.
    std::shared_ptr<std::set<std::string>> dimNames_;
    std::shared_ptr<std::set<std::string>> boundDims_;
    std::shared_ptr<std::set<std::string>> dimPacks_;
    std::shared_ptr<std::map<std::string, size_t>> dimUses_;
    bool dimBindAllowed_ = false;

    // Where the value of what is being lowered goes, as far as the warning
    // ExitInUnusedBranch needs it. A `return` in a branch arm ends the arm and
    // gives the value of the `if` / `match` (an arm is a graph of its own); when nothing
    // takes that value the statement has no effect at all, and the program most likely
    // meant to leave the function.
    struct ValueUse {
        // The `if` / `match` about to be lowered that is a statement whose value nothing
        // takes (not the last statement of its block, or the last one of a block whose own
        // value nothing takes).
        const AST::Node *unusedCtrl = nullptr;
        // The expression of the expression statement about to be lowered (an `if` without
        // `else` is allowed there only).
        const AST::Node *stmtExpr = nullptr;
        // Set for the block about to be lowered: nothing takes its value.
        bool blockUnused = false;
        // The unused `if` / `match` whose arm is being lowered: exits written here are its.
        AST::node_ptr_t unusedArmOf = nullptr;
    };
    ValueUse valueUse_;
    // Restores valueUse_ when a construct with bodies of its own is left, also by an error.
    struct ValueUseScope {
        Builder &self;
        ValueUse saved;
        explicit ValueUseScope(Builder &b) : self(b), saved(b.valueUse_) {}
        ~ValueUseScope() { self.valueUse_ = saved; }
    };
    std::set<std::pair<size_t, size_t>> reportedExits_;
    // The next block lowered is an arm of `ctrl`, whose value is unused or not.
    void enterArmOf(const AST::node_ptr_t &ctrl, bool valueUnused);
    void reportExitInUnusedArm(const AST::node_ptr_t &exit);

    void pushScope() { typeScope_ = std::make_shared<Scope<Reference, Type *>>(typeScope_); }
    void popScope() { typeScope_ = typeScope_->outer(); } // TODO: Shall we free the scope?

    // ast/base.h
    node_ptr_t visitModule(const AST::node_ptr_t &ast);
    void_ptr_t visitImport(const AST::node_ptr_t &ast);
    node_ptr_t visitExport(const AST::node_ptr_t &ast);

    // ast/stmt.h
    node_ptr_t visitStmt(const AST::node_ptr_t &ast);
    node_ptr_t visitDataDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitFuncDecl(const AST::node_ptr_t &ast);
    /// A `tag` declaration (design principles 6.2): the tag is registered here, before the types
    /// are declared, and lowers to no node.
    void visitTagDecl(const AST::node_ptr_t &ast);
    /// A `resource` declaration (design effects plan 3.2): the resource is registered in the
    /// module before any declaration is lowered, and lowers to no node.
    node_ptr_t visitResourceDecl(const AST::node_ptr_t &ast);
    /// The declaration each registered resource was written at, for the diagnostics of
    /// checkResourceGroups.
    std::map<std::string, AST::node_ptr_t> resourceDecls_;
    /// Checks the members of every group the module declares: a member that is neither a
    /// parameter nor a resource (UnknownResource) and a group that contains itself
    /// (ResourceGroupCycle) are reported here, before a clause expands one.
    void checkResourceGroups();
    /// The resources a function type's `uses`/`pure` clause publishes (design effects plan 3.1),
    /// in the numbering of `built`: parameters by class (a handle type a Handle, a `var` a
    /// Variable, any other an Object), ambient resources (a group expanded), or nothing for
    /// `pure`. Generic when the type writes no clause.
    sem::ResourceSignature
    resourceSignatureOf(const AST::node_ptr_t &funcTypeAst, const FunctionType *built);
    /// The ambient resources a name stands for, as written: a standard resource, one the module
    /// declares, one it imports, or a group of them (expanded). False when the name is none of
    /// those; `where` locates the diagnostic (UnknownResource, ResourceGroupCycle).
    bool resolveAmbientResource(
        const camel::core::module::module_ptr_t &in, const Reference &name,
        std::vector<std::string> &out, std::vector<std::string> &active,
        std::string *failed, const AST::node_ptr_t &where);
    /// The tag uses written before `ast`, lowered: the `RepeatedLoad("TagUse")` child of a
    /// function, type or block declaration, and the one of each of its fields.
    std::vector<GCT::TagUse> tagUsesOf(const AST::node_ptr_t &ast);
    /// Attaches the tag uses of `ast` (if it has any) to the node they are written on.
    void attachTagUses(const GCT::node_ptr_t &gct, const AST::node_ptr_t &ast);
    /// `func operator <symbol>(...)` (design principles 4.3): reports, and aborts, an operator
    /// that cannot be overloaded (OperatorNotOverloadable) and a declaration that does not fit
    /// its operator: the number of parameters, a with-parameter or a var parameter
    /// (OperatorOverloadInvalid). `funcData` is the declaration's FuncData, `decl` the whole.
    void checkOperatorDeclaration(
        const std::string &symbol, const AST::node_ptr_t &funcData, const AST::node_ptr_t &decl);
    node_ptr_t visitTypeDecl(const AST::node_ptr_t &ast);
    /// The parameters of a type declaration, as type variables in the scope pushed around its
    /// right-hand side, in parameter order. Reports a parameter that is repeated or that
    /// denotes a type already.
    std::vector<camel::core::type::Type *> bindTypeParameters(
        const AST::TypeDeclLoad *decl, const AST::node_ptr_t &ast, const std::string &name);
    /// `type Name<params> = enum { ... }`: the declaration is created and its name is in scope
    /// before the variants are read, so that a field can hold the enum being declared.
    camel::core::type::Type *
    declareEnumType(const AST::node_ptr_t &ast, const AST::TypeDeclLoad *decl);
    /// `type Name<params> = { field: T, ... }`: a named struct type (design principles 5.2).
    /// Its identity is this declaration (and, for a generic one, the type arguments); its
    /// fields are in the order written here, and every instance keeps that order. A field may
    /// hold the type being declared behind an array (`type Node = { children: Node[] }`): the
    /// name is in scope while the fields are read, as an initially incomplete instance that is
    /// completed before the declaration returns. A struct cannot hold itself by value, and
    /// recursion through a map or a set is not represented yet.
    camel::core::type::Type *
    declareStructType(const AST::node_ptr_t &ast, const AST::TypeDeclLoad *decl);
    /// The variants of the declaration, with the field types of `instance` (the written ones
    /// for the declaration's own instance).
    std::vector<camel::core::type::EnumVariant>
    buildEnumVariants(const AST::node_ptr_t &ast, const camel::core::type::EnumType *instance);
    node_ptr_t visitUsingNamespaceDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitExprStmt(const AST::node_ptr_t &ast);
    node_ptr_t visitExitStmt(const AST::node_ptr_t &ast);
    /// A block; one written with decorators is lowered by visitDecoratedBlock unless
    /// `decorators` is false (it lowers the block itself).
    node_ptr_t visitStmtBlock(const AST::node_ptr_t &ast, bool decorators = true);
    /// `@d { ... }` (language reference 7.6): LINK(0)[FUNC (decorated block)[TYPE, EXEC,
    /// ANNO...]], the block as a function of no parameters called where it is written. The
    /// EXEC is the block lowered as without decorators (its scope, its exits, its value).
    node_ptr_t visitDecoratedBlock(const AST::node_ptr_t &ast);
    /// The body of a function that writes `yield` of its own (design principles 5.4, language
    /// reference 7.3.1): the machine that hands out a stream. Its `var`s are cells of the
    /// function's own body, so every pull reads the same cells; `step` runs the body from the
    /// point the `pc` cell names until the next `yield`, and the stream is a closure over it.
    node_ptr_t visitGeneratorBody(const AST::node_ptr_t &funcAst, const FunctionType *funcType);

    // ast/data.h
    node_ptr_t visitData(const AST::node_ptr_t &ast);
    node_ptr_t visitUnaryExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitBinaryExpr(const AST::node_ptr_t &ast);
    /// `a && b` / `a || b`: the branch they stand for (design principles 4.2).
    node_ptr_t visitLogicalExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitReservedExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitIfExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitLoopExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitMatchExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitMatchType(const AST::node_ptr_t &ast);
    node_ptr_t visitStaticFor(const AST::node_ptr_t &ast);
    node_ptr_t visitStaticLoop(const AST::node_ptr_t &ast);
    /// How many things the head of a `static for` / `static loop` names, against how many the
    /// walked element gives: `fields T` gives the triple (name, type, tags) and every other
    /// element gives one thing (`indices ts` the position, a written sequence the element). A head
    /// may name fewer of them, never more; a head that names more is
    /// `SemanticDiag::StaticHeadNameCount`, read here because this is where the walked shape -- and
    /// so the number of parts -- is known. `shape` is what the head wrote after `in` and `named`
    /// the number of names it wrote, both for the message.
    void checkStaticHeadNames(const AST::node_ptr_t &ast, const std::string &shape, size_t named);
    node_ptr_t visitStaticAccess(const AST::node_ptr_t &ast);
    node_ptr_t visitLiteral(const AST::node_ptr_t &ast);
    node_ptr_t visitArrayData(const AST::node_ptr_t &ast);
    /// `[e for x in xs if c]`: `xs->filter<(x) => c>->map<(x) => e>` (design principles
    /// 4.2), built as those two calls are. The type of `x` is the element type of `xs`,
    /// which only the GIR builder knows: each of the two functions is written with a type
    /// variable for it and is instantiated where `map` / `filter` receive it.
    node_ptr_t visitComprehension(const AST::node_ptr_t &ast);
    /// `ast` lowered where a value of type `type` is expected: an array literal whose
    /// elements are to be put into `T?` / `T!` is built with the conversions in place, an
    /// array or struct literal takes the element (field) type its position gives it (design
    /// principles 5.1), and anything else is visitData(ast). `entered` is set when the value
    /// built is the value of the declared type itself (nothing is left to declaredValue).
    node_ptr_t visitDataInto(const AST::node_ptr_t &ast, Type *type, bool &entered);
    /// The array literal `ast` built with every element in `elemType`, the element type its
    /// position gives it (`let xs: float[] = [1, 2.0]`, design principles 5.1). A literal
    /// element becomes the element type exactly; an element that is not a literal keeps its
    /// type (the GIR builder reports it if it does not fit).
    node_ptr_t visitArrayInto(const AST::node_ptr_t &ast, Type *arrayType, bool &entered);
    /// A nested array literal at a position that states a rank of its own (`let b: f32[2, 2] =
    /// [[1.0, 2.0], [3.0, 4.0]]`): the brackets the literal writes are the dimensions of one
    /// array, so the value is a rank-N constant of that shape instead of an array of its rows
    /// (design principles 5.1, 5.4). A literal whose written shape is not the shape the position
    /// states — a flat `[1.0, 2.0, 3.0, 4.0]` is an `f32[4]`, not an `f32[2, 2]` — is reported
    /// and refused here: nothing is reshaped to fit.
    node_ptr_t visitRankedArrayInto(const AST::node_ptr_t &ast, Type *arrayType, bool &entered);
    /// The struct literal `ast` built with every field in the type its position gives it
    /// (`let p: P = { a: 1, b: 2.0 }`). Only when the literal writes exactly the fields of
    /// `structType`: any other shape is left to the ordinary build, which reports it.
    node_ptr_t visitStructInto(const AST::node_ptr_t &ast, Type *structType, bool &entered);
    /// The tuple literal `ast` built with every element in the type its position gives it
    /// (`let t: (f32, f32) = (1.0, 2.0)`). Only when the literal writes as many elements as the
    /// tuple type: any other shape is left to the ordinary build, which reports it.
    node_ptr_t visitTupleInto(const AST::node_ptr_t &ast, Type *tupleType, bool &entered);
    /// `node` as a constant of `type` when it is a literal the tree kept the exact value of
    /// (an integer literal becomes the float type exactly, a value out of range or one that
    /// would be truncated is reported at `at`); the same node when it is not a literal, or
    /// when the type is not one a literal can be given this way. Null-safe.
    node_ptr_t literalNodeInType(const node_ptr_t &node, Type *type, const AST::node_ptr_t &at);
    /// A text element of the composite literal `data` where the type of its position expects
    /// something else, reported as the type mismatch it is (design principles 5.1) at the
    /// element itself, as the same check reports an element whose value does not fit. Recurses through
    /// nested composites; false when there is no such element, and the caller reports the
    /// literal the way it did before. `where` is the position to use for an element whose own
    /// position was not kept.
    bool rejectTextInLiteral(
        const data_ptr_t &data, Type *type, camel::source::origin_id_t where);
    /// The initializer `value` of a declaration written with the type `type` (`let x: T = e`,
    /// lowered from `ast`). A literal is converted to the type here; any other expression is
    /// put under an annotation CAST, which the GIR builder resolves once the type of the value
    /// is known: it puts the value into a declared `T?` / `T!`, says which function a
    /// declared function type means, and otherwise checks that the value has the declared
    /// type (a value of that very type passes; nothing is converted).
    node_ptr_t declaredValue(const node_ptr_t &value, Type *type, const AST::node_ptr_t &ast);
    node_ptr_t visitStructData(const AST::node_ptr_t &ast);
    node_ptr_t visitTupleData(const AST::node_ptr_t &ast);
    /// `..e` (design principles 5.5): an SPRD holding the operand of the rest argument.
    node_ptr_t visitSpreadArg(const AST::node_ptr_t &ast);
    /// `{ static for... }` / `(static for...)`. an SCOMP holding the generator.
    /// The rounds are packed into a shape of the same fields in the graph builder.
    node_ptr_t visitStaticComprehension(const AST::node_ptr_t &ast, bool isStruct);
    /// `T { static for ... }`: an SCOMP holding the generator and the type node naming `T`.
    node_ptr_t visitShapeComprehension(const AST::node_ptr_t &ast);
    node_ptr_t visitFuncData(const AST::node_ptr_t &ast);
    node_ptr_t visitRefData(const AST::node_ptr_t &ast);

    // ast/type.h
    Type *visitType(const AST::node_ptr_t &ast);
    Type *visitStreamType(const AST::node_ptr_t &ast);
    Type *visitNullableType(const AST::node_ptr_t &ast);
    Type *visitTypeExpr(const AST::node_ptr_t &ast);
    Type *visitArrayType(const AST::node_ptr_t &ast);
    Type *visitStructType(const AST::node_ptr_t &ast);
    Type *visitTupleType(const AST::node_ptr_t &ast);
    Type *visitFuncType(
        const AST::node_ptr_t &ast, Tree::ParameterDefaults *defaultsOut = nullptr);
    Type *visitRefType(const AST::node_ptr_t &ast);
    Type *visitSpecType(const AST::node_ptr_t &ast);
    Type *visitMatchTypeType(const AST::node_ptr_t &ast);
    /// The extents of one `[...]` as linear forms (design principles 5.4). An unknown extent
    /// (`?`, or the empty `[]`) is `ExtentForm::unknown()`; nullptr with a diagnostic reported.
    std::shared_ptr<const std::vector<camel::core::type::ExtentForm>>
    visitExtents(const AST::DimGroup &group, const AST::node_ptr_t &ast);
    /// One extent as a linear form: a constant, a name, `+`/`-`, and `*` by a constant. Any
    /// other expression is unknown.
    camel::core::type::ExtentForm visitExtent(const AST::node_ptr_t &ast);
    /// `[...] as Map<K, V>`: an error when two constant keys of the literal are equal.
    void rejectDuplicateLiteralKeys(
        const data_ptr_t &data, Type *targetType, const AST::node_ptr_t &ast);
};

} // namespace camel::compile::gct

