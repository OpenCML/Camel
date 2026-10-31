# S1 — a Fibonacci stream

The showcase design's stream row (`docs/design/showcase/README.cn.md`, "流与驻留执行") asks for a
program that exercises `T..`/`yield` and gives the same result under a stream schedule as under a
batch one. This is the smallest program that does: a generator whose round carries **two** values
across the pull.

`fib.cml` is the whole example.

## The stream, term by term

| what the code does | what it means |
|---|---|
| `func fib(limit: int): int.. sync` | a generator: the head writes `int..`, a stream of `int`, and its body may `yield` |
| `loop a = 0, b = 1 while a < limit do { … }` | the loop carries `a` and `b` across the pull: two cells, each written where the loop stands |
| `yield a` | the pull stops here and hands out `a`; the statements after it run only when the consumer pulls again |
| `(b, a + b)` | the next round's pair, built from the round before it: the body's last value is the next state |
| `loop n = 0 for v in fib(50) do sync { println(v); n + 1 }` | a consumer: ten elements below 50 |
| `loop total = 0 for v in fib(50) do total + v` | a second call, hence a second stream, summing the same elements |

Calling `fib` again makes a new stream: a stream is linear, and a generator function may be called
any number of times.

## What the reading pins

`test/plans/feat/showcase_s1.plan.toml` pins the whole standard output — the ten Fibonacci numbers
below 50 and their sum, 88 — on the default back end and `std::prl`. Two mistakes a lowering can
make are visible in that sequence and nowhere else: dropping one of the two carried values, and
writing the second state before reading what the first one replaced (`s + i` reading the new `i`).
The second mistake does not crash; it prints a different sum.