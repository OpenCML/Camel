// The assertion the deletion of a helper needs: the name is gone from the ruleset, and the key
// that used to reach it still reaches its replacement. A helper of a ruleset module is private --
// no program imports it -- so a Camel file that named it would fail whether or not it existed, and
// the honest reading of "the function no longer exists" is the source. What a Camel case *can*
// prove is the other half: the batched call still answers (test/cases/compile/transforms/
// vmap_row_reductions.cml), so the deletion did not turn a rule into a refusal.
//
// Run from the repository tree the plan hands the case to (`node <this file>`).
import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))

// The case is copied into the tree the run happens in, so the repository root is the first ancestor
// that holds the ruleset the case is about.
function repositoryRoot(start) {
    let dir = resolve(start)
    for (;;) {
        try {
            readFileSync(join(dir, 'stdlib', 'rulesets', 'vmap.cml'), 'utf8')
            return dir
        } catch {
            const parent = dirname(dir)
            if (parent === dir) throw new Error(`no stdlib/rulesets/vmap.cml above ${start}`)
            dir = parent
        }
    }
}

const root = repositoryRoot(here)
const ruleset = readFileSync(join(root, 'stdlib', 'rulesets', 'vmap.cml'), 'utf8')

const deleted = ['fold_max_axes', 'fold_min_axes']
for (const name of deleted) {
    if (ruleset.includes(name)) {
        throw new Error(`${name} is still in stdlib/rulesets/vmap.cml: the duplicate was not deleted`)
    }
}

// What replaced them: the operator, written once each. The exact spellings are the point -- a
// different reshape or a fold of axis 0 would answer another shape and another set of numbers.
const replacements = [
    ['export func vmap_max::<T>(xs: T[...]): T[...]', 'max<1, false>(reshape<[shape(xs)[0], -1]>(xs))'],
    ['export func vmap_min::<T>(xs: T[...]): T[...]', 'min<1, false>(reshape<[shape(xs)[0], -1]>(xs))'],
]
for (const [head, body] of replacements) {
    const at = ruleset.indexOf(head)
    if (at < 0) throw new Error(`no ${head.slice('export func '.length, 9)} in the ruleset`)
    const end = ruleset.indexOf('\n}', at)
    if (!ruleset.slice(at, end).includes(body)) {
        throw new Error(`${head.slice('export func '.length, 9)} does not write ${body}`)
    }
}

// The keys are intact and still name the functions the transform calls: a deleted helper is not a
// deleted rule, and the batched call has to keep working through the same key.
const keys = [
    ['max', 'vmap_max'],
    ['min', 'vmap_min'],
    ['sum', 'vmap_sum'],
    ['mean', 'vmap_mean'],
]
for (const [head, callee] of keys) {
    const rule = new RegExp(`rule vmap_rule_${head}::<T>\\(xs: T\\[\\.\\.\\.\\]\\)[^{]*\\{\\s*${head}\\(xs\\) => [^\\n]*${callee}\\(bxs\\)`)
    if (!rule.test(ruleset)) {
        throw new Error(`the key ${head}/1 no longer names ${callee}`)
    }
}

// `sum` and `mean` keep their fold: folding a float row's axes rounds once per axis and flattening
// rounds once, which is a different number. Their helpers must therefore still be there.
for (const kept of ['fold_sum_axes', 'fold_mean_axes']) {
    if (!ruleset.includes(kept)) {
        throw new Error(`${kept} was deleted too: a float row folded twice is not one rounding`)
    }
}

console.log(`deleted_helpers: ok (${deleted.join(', ')} gone; sum/mean folds kept)`)