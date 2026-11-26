# 判 debugger 的红：`camel-db` 的 REPL 三行命令

跑测试计划一轮要 300 秒以上（`feat.tools.debugger` 里 `dap_stepping` 一条就 242 秒），而
debugger 自己带着一个不经过 DAP 的入口：REPL。判一条 debugger 的红在**哪一层**，先用它，
10 秒一轮，不用起 DAP、不用重建、不用 `.mjs` 客户端。

## 三行命令

```sh
cd <tree>/test/cases/tools/debugger/fixtures
printf 'break values.cml:25\nrun\nlocals\nquit\n' | <tree>/out/latest/bin/camel-db values.cml
```

（`CAMEL_HOME=<tree>/out/latest`；`printf` 的每一行就是 REPL 的一条命令。）

## 期望输出（当前尖端实测，`values.cml` 第 25 行的断点）

```
Breakpoint 1 at values.cml:25
(camel-db) run
Run started (task 60939).
Running /…/fixtures/values.cml
Stopped (breakpoint) in main  values.cml:25:5
(camel-db) [Locals]
  n: int = 42
  ratio: float = 2.5
  ok: bool = true
  name: string = 'camel'
  xs: int[] = (3) [1, 2, 3]
  pair: (int, string) = (7, seven)
  point: { x: int, y: int } = { x: 1, y: 2 }
  counter: int = <not evaluated>
  some: int? = 9
  none: int? = null
  good: int! = 25
  bad: int! = error DivisionByZero: Attempted division by zero.
  table: Map<string, int> = <not evaluated>
  later: int = <not evaluated>
```

## 为什么这比跑计划值

- **一轮 10 秒**：同一份 `[Locals]` 一次性把「哪几个值读不出来、类型对不对」摆出来，不必等
  `dap_session` 的 101 条断言跑完。
- **省一层**：DAP 客户端、HTTP 网关、超时都在别处，读不到值就是读不到值，不会和它们混在一起。
- **可复现**：命令里没有时间、没有任务号（`task 60939` 每次不同，不要拿去当断言）。

## 判定口径（三种读数各归哪一层）

| 读数 | 归哪一层 |
| --- | --- |
| 某个值 `<not evaluated>`，类型也是内层类型（`var int` 退成 `int`） | debugger 的值解析（名字→节点、槽位存活） |
| 值对、类型错 | 类型的呈现（`values.cpp` 的 `typeText`） |
| 整个 `[Locals]` 空 / REPL 起不来 | 进程或适配层（先看 `camel-db` 是否在 `out/latest/bin`） |

用同一份读数做前后对照：改前贴一次、改后贴一次，差值就是改动的效果，不必靠计划里的断言名去猜。