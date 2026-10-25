# The position a pass name has when it is not a position in any file

> 说明（协调者）：提出问题时的代码在本地**删 `module` 声明那条线** `handover/h554-no-module-decl`（尚未并入 develop、也未推送）；页里引用的行号与读数以那条线为准。


Four tests have been failing in every gate of this branch (`unknown_pass_stage`,
`pass_parameter_stage`, `camel_pass_parameter_unknown`, `camel_pass_parameter_wrong_type`). They are
the only family left that no one has attributed to a change: they were already failing in gate 11,
the first gate after `origin/develop` was merged into the removal of the module declaration, and they
have failed in every gate since (11, 12, 13, 14 each contain three of them). The question is what
position a pass specification should report, because a pass name written after the entry file is not
written in any file.

## What it is

`camel program.cml std::opt::saturate foo=bar` names passes on the command line. When a pass name is
unknown, or a pass parameter is wrong, the diagnostic has to say *where* the mistake is, and there is
no file position for it: the words live on the command line. The compiler gives such a diagnostic a
synthetic position. Two things are in question: what that synthetic position is (which line and
column), and whether `--output-format json` should carry it at all.

## The evidence, verbatim

`camel_pass_parameter_unknown` and `camel_pass_parameter_wrong_type` pin `line 0`, and the compiler
prints line 1:

```
[INFO] expected diagnostic SemanticDiag::ParamArgumentUnknown, line 0, column 30
[INFO] expected diagnostic SemanticDiag::ParamArgumentWrongType, line 0, column 26
```

`unknown_pass_stage` reads the JSON form and finds a range of zeros rather than no range:

```
[INFO] verification output: Error: no source range
[stdout] {"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"severity":"Error","code":"0x0400000A","source":"Camel","message":"Unrecognized graph rewriting/scheduling…
```

`pass_parameter_stage` fails its own verification the same way. The compiler's own printed position
for a pass parameter problem is `(pipeline):1:<column>` — line 1, not line 0.

## What it reads today

Today a pass specification that does not resolve is reported at `(pipeline):1:<column>` for the text
forms, and the JSON form carries `start = end = {line: 0, character: 0}`. The four tests between them
expect: `line 0` for the text form (two tests), and for the JSON form a range that is either absent or
not the all-zero range (one test reads `no source range`, one fails on a stage field).

## Options and what each costs

1. **Line 0 is right and the compiler is wrong** (the current pins win): the synthetic line becomes
   0-based like every other position `--output-format json` prints, and the text form prints
   `(pipeline):0:<column>`. Cost: every message that quotes a pass position changes; the pipeline
   stage is not a file, so "line 0" is a convention, and the language reference does not describe
   synthetic positions today, so the convention would have to be written down somewhere normative.
2. **Line 1 is right and the pins are stale** (the compiler wins): the four pins become `line 1`, and
   the JSON test stops asking for a range the compiler does not have. Cost: nothing in the code
   changes, but four tests are then asserting a number that no document states, and the next person
   reads them as an accident.
3. **A pass name has no position at all**: the diagnostic carries no range in JSON and prints no
   `(file):line:column` in text, naming the pass and the command line instead. Cost: the biggest
   change of the three, and it removes the position from the one place a user can still see which
   word was wrong.

## What it blocks

Four tests, and nothing else: no other failed test in this branch depends on this decision. It also
blocks the honest answer to "is `(pipeline)` a position the language has", which the language
reference does not answer today.