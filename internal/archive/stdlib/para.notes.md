# para 重写说明（并含 test_support）

稿件：`para.cml`、`test_support.cml`。未编译。

## 判断

`para` 里三个东西性质不同：
- `unordered_foreach`、`unordered_reduce`：真正的库函数。它们是 `foreach`、`reduce` 的"元素之间不定顺序、可以同时进行"的版本，设计原则 3.6 点了名。**留在 `para`。**
- `rendezvous(parties, timeout_ms)`：让若干个调用互相等待，用来证明"调用真的同时在跑"。这是**运行时测试的设施**，不是用户程序会用的东西。**移出标准库**，到 `test_support`（conventions 6 的"工具"层已列出此名）。

## 改了什么

1. `para`：删 `module para` 行与 `@effect` 标签；说明里写明"效应是回调的，顺序不定"。两个函数的 `#op(':mark/...')` 保留（它们是内建的元素调用，URI 不是 `para:...`）。没有资源子句：和 `map`/`foreach`（`stdlib/signatures/globals.cml`）一样，回调的资源由实例化决定，原生算子本身不增加。
2. `rendezvous`：搬到 `test_support`，超时单位由毫秒（`timeout_ms: int`）改为秒（`timeout: float`），与全库一致；资源由 C++ 的 `para:rendezvous` 改为模块里声明的 `rendezvous_point`（`export resource rendezvous_point`），`uses rendezvous_point`。
3. 参数不分 `with`：`rendezvous(parties, timeout)` 没有"流过的数据"，两个都是普通参数。

## 函数表

| 项 | 处理 |
|---|---|
| `unordered_foreach`、`unordered_reduce` | 保留（`para`） |
| `rendezvous` | **搬到 `test_support`**，改秒 |

## C++ 侧改动

- `modules/para`：只剩两个元素调用的算子元数据，没有自己的执行器；`rendezvous` 的实现与 `declareResources` 搬到新模块 `modules/test_support`（URI `test_support:rendezvous`）。
- `OperatorTable` 里 `para:rendezvous` 的资源名 `para:rendezvous` 改 `test_support:rendezvous_point`。

## 调用点

| 变化 | 位置 |
|---|---|
| `rendezvous(n, ms)` → `import { rendezvous } from test_support`，`rendezvous(n, 秒)`（约 7 个文件） | `test/cases/para/core/{concurrent_calls,concurrent_recursion,merge_sort_small,sync_order_with_tasks}.cml`、`test/cases/compile/native/para_{arity,declaration_import,type}.cml` |
| `unordered_*` 不变 | `test/cases/para/core/` 其余 |
| 清单类 | `docs/stdlib-reference.md`（生成）、`docs/reference/camel-for-ai-stdlib-signatures.md` |

## 已定的两点

1. `unordered_foreach` 的回调返回 `void`（它为效应而存在，与 `foreach` 同形）；`test/cases/para/core/map_chunks.cml` 里 `(x: int) => x` 的回调要相应改。
2. `thread_count` 搬进 `test_support`（`uses process`）：核对了全仓库，只有测试在用（`test/cases/modules/tensor/{thread_count,thread_pools}.cml`、`test/plans/feat/modules/threads.plan.toml`）。`process.cml` 不再有它，C++ 里把 `process:thread_count` 改为 `test_support:thread_count`。
