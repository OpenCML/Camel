# `RewriteInvalid`: the two writings' types are one type by text and two by address

Question raised by the lane `handover/h478-facts-and-fold` (H478) on 2026-10-25, on its integration
tree (`h465`'s tip `5116c47e6` merged with `origin/develop` `a7a2badc6`). Everything below is verbatim
from that tree. The lane changed no semantics: this file is the measurement and the question.

## 1. What is red

`test/plans/feat/modules/rules.plan.toml:841-850`:

    name = "rules.tensor_saturation_keeps_values"
    desc = "The tensor saturation (both ways, including the i64 distribution over tensor add) rewrites
    the same program to writings that still compute the values of the program as written: the point of
    a two-way law is that either direction is the same value."
    args = ["${case}", "rules::saturate_all", "std::nvm"]
    expect_exit = 0
    expect_stdout_contains = ["[8.0, 16.0, 24.0]"]

The h445 lane recorded this red as "a real defect of the rewrite engine's permission check rather than
a missing arm" (`docs/handover/questions/z-bucket-print-form-and-unstated-leaf.md:111-116`, citing
`src/runtime/writings.cpp:779`). The reading below says the permission check is **not** what fails:
the type comparison is by address.

## 2. A five-line reproduction

    $ cat /tmp/h478/sub0.cml
    module t_sub
    import { Tensor } from tensor
    type Vec = Tensor<f32, [3]>
    func sub0(x: Vec): Vec { return x - 0.0 }
    func main(): int sync { return 0 }

    $ ./out/latest/bin/camel /tmp/h478/sub0.cml "rules::saturate_all" std::nvm
    error[RuntimeDiag::GraphPassFailed]: Pass 'rules::saturate_all' failed: error RewriteInvalid:
    Two writings of a value have the same type, region and permissions, and the permissions the
    equivalence needs..
    help: A pass written in Camel returned an error value instead of a program, and the message is the
    error's kind and text. The program is left as the previous pass produced it, and the pipeline stops.

`x - 0.0` matches exactly one law, `stdlib/rules.cml:269`
`export rule tensor_f32_sub_zero(x: Tensor<f32>) { x - 0.0 => x }`, which declares no permission.
`x * 1.0` (`:267`), `x + -0.0` (`:268`) and `(x * 1.0 + -0.0) - 0.0` fail the same way; `x + x`
(`tensor_f32_add_self`, `:274`), `x * c + x * c` (`tensor_f32_double`, `:291`) and `x * 3.0 + x * 5.0`
(`tensor_f32_combine`, `:277`) do not. `rules::simplify` on the plan's own case prints the values the
plan expects, so the saturation's answers are right where it gets through.

## 3. The condition that is failing

`src/runtime/writings.cpp:772-784`, verbatim:

    bool DraftWritings::assertEqualDeferred(
        gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms, uint16_t rule) {
        const auto *ha = draft_.header(a);
        const auto *hb = draft_.header(b);
        if (ha == nullptr || hb == nullptr || ha->dataType != hb->dataType ||
            draft_.nodeRegion(a) != draft_.nodeRegion(b) || ha->permissions != hb->permissions ||
            (perms & ~contextOf(a)) != 0) {
            throw std::logic_error(
                "Two writings of a value have the same type, region and permissions, and the "
                "permissions the equivalence needs.");
        }
        return store_.merge(a, b, perms, rule, 0, this);
    }

A temporary probe (since reverted) printed every term of that condition:

    [INFO] <DraftWritings> ASSERTEQUAL a=2 b=0 typeA='f32[3]'@0x7684c8137a60 typeB='f32[3]'@0x7684c81377f0
      sameTypePtr=false regionA=0 regionB=0 permsA=0 permsB=0 needed=0 contextA=3 rule=5
    [INFO] <DraftWritings> ASSERTEQUAL a=2 b=0 typeA='f32[3]'@0x702c2fb37a60 typeB='f32[3]'@0x702c2fb377f0
      sameTypePtr=false regionA=0 regionB=0 permsA=0 permsB=0 needed=0 contextA=3 rule=3

The first line is `sub0.cml`; the second is `neutral(x: Vec) = (x * 1.0 + -0.0) - 0.0` from the earlier
bisect. Both writings print `f32[3]`, both are in region 0, both carry permissions 0, and the law
needs no permission at all (`needed=0`; `contextA=3` is a context that would grant one). **Every term
of the condition holds except `ha->dataType != hb->dataType`**: the two `Type *` are distinct objects
of one and the same type.

`a=2` is the node the operator built (`x - 0.0`), `b=0` is the parameter `x`: the first type object
comes out of the operator's own inference, the second is the one the declaration `Vec = Tensor<f32, [3]>`
gave the parameter. The rule indices corroborate which law fired (`3` for `neutral`, whose first match
is `tensor_f32_mul_one`, `5` for `sub0`'s `tensor_f32_sub_zero` — the declaration order of
`stdlib/rules.cml:267-269`).

That `Tensor` is an alias of the native array type (P4; `modules/tensor/type.cpp:27-33`) is what put
an *array* type on both sides. `ArrayType::create` / `ArrayType::openRank` build a fresh object for a
structurally identical type, and `src/runtime/draft_inline.cpp:157-163` shows the same two calls
making a fresh `f32[..]` at every call site — that is the mechanism of
`docs/handover/questions/recursive-type-specialization-key-is-never-the-same-key.md`, filed by this
same lane. **The two reds look like one defect: types are compared by address where their content is
what is meant.**

## 4. Questions for the author

1. Is a rewrite's equivalence check meant to compare the two writings' types by address, or by what
   the type *is*? `ha->dataType != hb->dataType` (`writings.cpp:776`) is a pointer comparison, and
   nothing interns the array types that are built per node.
2. If it is meant to be structural, which is the ruling: a structural comparison here, interning of
   array types, or "a rule states nothing about the type and the merge is allowed when the two types
   are one type by text"? This lane changed nothing.
3. `tensor_f32_sub_zero` is a one-way rewrite (`=>`) with no declared permission. Is a rule with no
   permission supposed to go through the equivalence check at all, or is the check for `<=>` laws?

## 5. What this lane did instead

Left `rules.tensor_saturation_keeps_values` red, changed no semantics, and left the plan's expectation
(`[8.0, 16.0, 24.0]`, the values of the program as written) exactly as it was.