# A Python project whose rules are a Camel module

[中文简体](README.cn.md) | English

The pricing rules of a small shop live in [`rules.cml`](rules.cml), and the project around them is
Python: it reads the baskets, keeps them in NumPy, prints the report. This is the adoption path
H253 was written for -- "use a part of Camel in a Python project", not "move the project".

```sh
pip install camel-lang
python examples/python/rules/basket.py
```

`pip install` is what a project does once; the second command is what it runs. Nothing else is set
up: the wheel carries the runtime and the standard library under `camel/.cmlenv/`, and the package
points `CAMEL_HOME` at it (`camel/_native.py`). Fields of the binding -- the array crossing, the
exceptions, the extension module -- are documented in
[docs/technical/57_python_bindings.md](../../../docs/technical/57_python_bindings.md).

## What the run prints

```
rules module: rules from examples/python/rules/rules.cml

  basket    discount   shipping   net total   net (Python)
     80.00     0.00%      9.90       89.90          89.90
    120.00     5.00%      9.90      123.90         123.90
    480.00     5.00%      0.00      456.00         456.00
    620.00    10.00%      0.00      558.00         558.00
   1200.00    15.00%      0.00     1020.00        1020.00
   2500.00    20.00%      0.00     2000.00        2000.00

1,000,000 baskets, one NumPy array of f32 (4 MiB)
  Camel:          1264.1 ms, rules as functions in rules.cml
  plain Python:    443.2 ms, the same rules written again here
  NumPy:            29.9 ms, the same rules as array expressions
  agreement:    Camel against Python 0.00e+00, against NumPy 0.00e+00 (largest net total here is 239999)
```

(Times from one run on the reference Linux host; the two agreement figures are the point that does
not move.)

## What the example does and does not claim

The rules in `rules.cml` are checked when the module loads. A typo in a rule is a diagnostic with a
file, a line and a column, raised at `camel.load`, not a branch that fails in production a month
later -- the test suite pins that shape in `test/cases/feat/bindings/python_binding.py`. The array
entry point takes a NumPy array through DLPack, so the million baskets above are not copied on the
way in: the binding's own test measures the crossing at 12.9 us for 64 MiB against 4818 us to copy
the same bytes.

It does **not** claim that this is the fast way to price a million baskets. The numbers above say
the opposite, and they are in the example because the honest answer is in them: this rule set fits
into a NumPy expression, and NumPy is forty times faster at it. The module runs on the node VM,
which is an interpreter that calls a closure per element, so a rule set that is a tight arithmetic
loop belongs in NumPy. A rule set stops fitting there when it grows past what array expressions
hold -- records instead of scalars, rules that can fail (`T!`), rules that call other rules,
decisions that need a `match` -- and then the choice is between a Python function per rule (checked
when it runs) and a Camel module (checked when it loads). That is the part worth writing in Camel,
and this example is the smallest one that shows the whole path working.