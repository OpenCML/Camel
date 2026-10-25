# A run-time refusal that the ruling counts as compilation errors

> 说明（协调者）：提出问题时的代码在 `handover/h554-no-module-decl`（基点 `origin/develop`，不含 P4 的数组内存迁移）；拒绝的实现与 develop 同源，读数以该线为准。


The author's ruling on the gradient refusal asks for **one diagnostic per rejected call**, "like one
compilation collecting all its errors, so the reader does not have to fix one before seeing the
next". Reading the code for that change turned up a conflict the ruling does not cover: the refusal
is not a compilation error at all — it is thrown as a **run-time fault**, and a run-time fault ends
the run at the first fault, which is a rule the command line document states in its own words. So
"one diagnostic per rejected call" and "a run ends at its first fault" cannot both hold while the
refusal stays where it is.

## What it is

`modules/autodiff` decides whether a call of a function value can be differentiated while the program
runs (the graph is already built, the value is already flowing). When it cannot, it throws:

```
modules/autodiff/pullback.cpp:84: throwRuntimeFault(RuntimeDiag::RuntimeError, "autodiff: " + message);
```

and the message is built per call already — `Backward::refusalReason(callee_or_call)`
(`pullback.cpp:3304`) says "cannot differentiate through **this call** of a function value: …", and
`whereIs(origin)` (`pullback.cpp:149-163`, format `" (at {}:{}:{})"`) appends the place of the call.
What is missing is not the wording; it is that only one call is ever reached, because the first
throw ends the run.

The command line document says, of the same run-time faults (`docs/cli.cn.md:107`, English
`docs/cli.md`): a run-time fault always ends the run at the first fault. The JSON form of a run-time
fault carries one diagnostic for the same reason.

## The evidence, verbatim

The message a reader sees today, from the failing case (one call is named, and the run stops there):

```
(grad_function_value_branch)…/grad_function_value_branch.cml:10:13: [Error]: An error has occurred: autodiff: cannot differentiate through this call of a function value: which function it calls is not evident where it is called: the function is chosen by a branch at run time (at …/grad_function_value_branch.cml:10:13). A call of a function value is differentiated when the function is named, or built by a lambda, in the function being differentiated or in a function it calls, and gets from there to the call by being bound, captured, passed as an argument or returned. (name=RuntimeDiag::RuntimeError, code=0x04000000)
```

The rule it runs into, from the command line document:

```
运行期故障总是在第一个故障处结束运行。
```

The twelve cases expect two positions in one output (`10:13` and `11:13`) and read the message for
the call it names.

## What it reads today

Today: the refusal is a run-time fault, the run stops at it, and one diagnostic names one call. The
ruling's first clause (one per call) is therefore not a local edit in `pullback.cpp`; it decides when
the refusal is found — at run time, once per run, or earlier, when every call of the value is still
in view.

## Options and what each costs

1. **Find the refusal while compiling** (a semantic check over every call of a function value that
   reaches a `grad`, reporting each rejected call with its own position and a related location where
   the value is chosen): the ruling's wording is met exactly, because compilation collects all its
   errors. Cost: a new semantic analysis that has to agree with what autodiff later decides in the
   graph, or the two will disagree on some program; and the cases move from a run-time expectation
   to a compile-time one.
2. **Keep the refusal at run time and report every rejected call of the value before stopping**: the
   run-time fault carries several diagnostics. Cost: it contradicts "a run ends at its first fault",
   and a program that faults early would be analysed further than it ran.
3. **Keep one diagnostic per run** (today's behaviour) and read the ruling as being about the
   *message* only: drop `(at …)` from the text, put the call's place in the position field and the
   place the value is chosen in as related information, and record one call per run. Cost: the twelve
   cases' "two positions" assertion cannot be met, and the ruling's first clause is then not
   implemented.

## What it blocks

The twelve `grad_function_value_*` cases, and the sentence the language reference's autodiff section
would carry (which sentence is right depends on the answer: a compile-time rule can say "every call
that cannot be differentiated is reported", a run-time one cannot). It does not block the other work
of this branch.