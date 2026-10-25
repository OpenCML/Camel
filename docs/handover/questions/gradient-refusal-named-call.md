# Which call a gradient refusal should name, when the function called is chosen by a branch

> 说明（协调者）：提出问题时的代码在本地**删 `module` 声明那条线** `handover/h554-no-module-decl`（尚未并入 develop、也未推送）；页里引用的行号与读数以那条线为准。


Twelve tests (`grad_function_value_branch`, `_container`, `_parameter`, `_passed`, `_recursion`,
`_static_branch`, each in a `default` and a `macro` back end) fail on one assertion with the same
shape. The author's ruling that a tangent is settled field by field, and that a wholly discrete
model gets a wholly zero, same-shaped gradient, does not decide this: it decides the value of the
gradient, and these tests fail before any value is compared.

## What it is

When autodiff refuses a function value whose callee is chosen at run time, it reports two places: the
diagnostic itself, and the call the message names as the one that cannot be differentiated. One case
has the value called twice, and the question is which of the two calls the message should name — the
first one, the one the diagnostic is located at, or every one of them.

## The evidence, verbatim

The test's first assertion pins the diagnostic, and the compiler agrees with it:

```
assert(text.includes('grad_function_value_branch.cml:10:13: [Error]'), 'the diagnostic is not located at the call (10:13): ' + text)
```

The second assertion pins a *different* line for the call the message names:

```
assert(/[(]at [^ ]*grad_function_value_branch[.]cml:11:13[)]/.test(text), 'the message does not name the call: ' + text)
```

What the compiler prints is the diagnostic's own position, in both places:

```
[INFO] verification output: Error: the message does not name the call: … grad_function_value_branch.cml:10:13: [Error] … (at …/grad_function_value_branch.cml:10:13)
```

So the message points at 10:13 while the test expects 11:13. The case has two calls of the value, at
10:13 and 11:13, and the message names the first.

## What it reads today

Today the diagnostic and the named call are the same position, always. The test wants them told
apart, and no reading in this repository says whether the distinction is meaningful — the language
reference's autodiff section does not say which call a refusal names, and no other test pins two
different positions for the two.

## Options and what each costs

1. **The message names the call the diagnostic is located at** (today's behaviour, and the first
   call): the tests' second assertion becomes the same position as the first. Cost: the message adds
   nothing the diagnostic's own position does not already say, and the assertion is then a duplicate.
2. **The message names the other call** (the tests' expectation): the compiler gains a rule that a
   refusal about a function chosen at run time points at the call it *did not* take first, or at the
   last one. Cost: a rule with no stated reason, and a behaviour that changes with the number of
   calls, so a one-call program has nothing to name.
3. **The message names every call of the value** (a note per call, or one note listing them): the
   compiler reports all the places the reader has to look. Cost: the largest change of the three, and
   the diagnostic grows a list whose length the program decides.

## What it blocks

Those twelve tests, and nothing else: no other failed test in this branch depends on this decision.
It also blocks the sentence the autodiff section of the language reference would need either way.