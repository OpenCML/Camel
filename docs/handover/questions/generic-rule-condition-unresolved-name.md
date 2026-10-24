# A generic rule whose condition does not resolve: quiet, or a diagnostic?

Status: open. Raised by the `check`-on-generic-rules lane (H401, `handover/h401-check-generic`),
2026-10-24, while adding the test that every exported rule of `stdlib/rules.cml` enters `check`.
Owner: language author (the ruling of 2026-10-23 on
`generic-rule-instantiation-set-and-predicate`, §3, is what makes the failure silent, and this asks
whether a *name error* is covered by it).

## What is measured

A module that declares one rule whose condition names a predicate nobody declares and nobody
imports:

```camel
export rule silent_rule::<T>(x: T) { x + 0 => x } when no_such_predicate::<T>()
```

and the same rule with `is_integer` imported as a control, on the host build of this lane
(`CAMEL_HOME=…/h401-check-generic/out/latest`), 2026-10-24:

```
$ camel probe_silent_when.cml std::nvm
ok loud_rule<int>: the condition selects the instances, fired 10 of 10
ok loud_rule<i32>: the condition selects the instances, fired 10 of 10
report done
EXIT=0
```

`silent_rule` produced **no line at all** -- not a verdict, not an `unchecked`, not a `sampled`. The
module compiled (exit 0) and the check ran. Asked about the value it would have rewritten
(`pair_target.cml`, `i64_pair`):

```
$ camel pair_target.cml probe_silent_when2::why std::nvm
rewrites 0 rule silent_rule
`silent_rule` is a generic rule, and no instance of it could be made for 'int':
Unresolved reference: 'no_such_predicate'.
EXIT=0
```

So the reason is known, exact and reachable (`why_not` prints it), the engine is stopped by it
(`rewrites 0`), and the two surfaces a reader would use to notice it -- the loader and `check` -- say
nothing. A misspelled predicate in a rule's condition is therefore indistinguishable from a rule
whose condition is simply never true: the rule is dead, the suite is green, and `check`'s report has
no line for it. The test this lane added asserts that every exported rule reaches a line of the
check's report; such a rule fails that test only because it reaches *no* line, and the reason is not
in the report.

## Why the ruling does not answer it

The ruling of 2026-10-23 (§3) makes a **failed instantiation** silent: an instance that cannot be
written for reports nothing where it happens, and `why_not` is where the reason is read. That is
about a body that a type cannot hold (`operator '-' is not defined for these operand types`), which
is a fact about the law and the types. A condition that calls a name no module declares is a
different kind of fact: a name error in the rule's own declaration. The ruling does not say whether
the same silence applies, and today it does.

## Options

**(a) Resolve the condition's names when the rule is declared.** The module stops loading with
`Unresolved reference: 'no_such_predicate'` at the rule's line, as any other unresolved name does.
Cost: the condition currently becomes a graph only when it is first asked (`settleGenericRuleMaker`
settles the templates, and the condition is evaluated inside a `SpeculativeInstantiation`); eager
resolution must build or at least name-resolve the `when` template for every declared rule in every
module, which is work no program pays today. Risk: a library that declares a rule for an optional
import (a condition naming an operator from a module the program may not load) would stop loading;
the front end would have to say which names a condition may use, or the rule would have to declare
them.

**(b) Keep the load quiet, and let `check` report it as a rule it could not examine.**
`check` prints one line per rule whose condition's template was never built, with the reason it has
(`unchecked silent_rule: its condition does not resolve: Unresolved reference: 'no_such_predicate'`).
Cost: the maker has to keep the reason of a template that failed to build (today the failure is
dropped where it happens, per §3, and `check` has nothing to print), and `check` has to tell "the
condition did not resolve" apart from "no instance of this law can be made" -- the second stays
silent, which is the ruling's point. This changes no load semantics and no engine behaviour: it makes
the failure visible exactly where coverage is asserted.

**(c) Leave it as it is.** A condition that does not resolve is caught by asking `why_not`, and by
the rule never firing. Cost: nothing notices a typo until someone asks that specific question about
that specific rule and value; `check`, the pass whose whole purpose is to say what the library's
rules do, is silent about it, and a rule that is dead by a typo passes the coverage test this lane
added only if the test treats "no line" as failure (it does) -- without saying why.

## Recommendation

(b), with (a) as the better long-term answer. (b) is small, costs one kept string, and puts the
reason where the coverage assertion already looks; (a) is the honest reading of "a name error is a
name error", but it changes when a module can load and needs a rule about which names a condition
may name, which is a language question and not this lane's.

## Sources

* `docs/handover/reviews/questions-2026-10-23.md`, ruling §3 (a failed instantiation is silent;
  `why_not` is where the reason is read) and the ruling on
  `generic-rule-check-surface-and-multi-parameter-instances` of 2026-10-24.
* The measurements above; probe sources `probe_silent_when.cml` and `probe_silent_when2.cml`
  (`D:\Projects\Camel-wt\notes\h401-probes\`, not committed -- they are three-minute reproductions).
* `test/plans/feat/modules/rules.plan.toml`, `rules.every_exported_rule_enters_the_check`, and
  `test/plans/feat/modules/verify/check_covers_every_rule.mjs`: the test that fails on a rule which
  reaches no line of the check's report.