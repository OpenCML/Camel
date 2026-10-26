# env 重写说明

稿件：`docs/handover/stdlib/env.cml`。未编译。

## 改了什么

- 删 `module env`，删 `@effect` 标签，签名写 `uses env`（C++ 现为 `externalAmbient("env", Read)`，一致）。
- **`get_env(name): string?`**：变量没设时原来返回 `''`，与"设了但是空"分不开，是哨兵值；改为 `null`。已设为空串的变量给 `''`。数据是变量名，所以 `'HOME'->get_env ?? ''` 可以链式。
- **`get_cwd(): string!`**：工作目录被删除是环境造成的失败，原先 C++ 里会得到什么取决于平台；改为 `string!`（`NotFound`/`PermissionDenied`）。调用点只有 8 处。
- 模块说明的开头"Both functions…declared with effects"删除（那是实现旁白）。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `get_env` | 改：`string?` | 去哨兵 |
| `get_cwd` | 改：`string!` | 环境失败 |

没有新增：设置环境变量会改变子进程与别的线程看到的世界，是另一个资源写入，需要先有 `process` 的子进程设计；`env_vars()` 整表可以用到再加。

## C++ 侧改动（`modules/env`）

- `__env_get_env__`：区分"未设"与"设为空"。现在靠 `::getEnv(...)` 返回空串判断（`modules/env/operators.cpp`），拿不到区别；`camel/utils` 的 `getEnv` 需要改为返回 `optional<string>`（Windows 的宽字符接口 `GetEnvironmentVariableW` 本来能区分，POSIX 的 `getenv` 返回 `nullptr` 能区分）。返回类型 `string?`，空值走 `NullSlot`。
- `__env_get_cwd__`：`currentDirectory()` 失败时 `makeError`，返回 `string!`。
- `module.cpp` 的资源与纯度声明不变。不需要 `stdlib/signatures/env.cml`（没有计算签名）。

## 需要迁移的调用点

`get_env` 一共约 196 处，其中**绝大多数是测试里取输出路径**（`get_env('ONNX_OUT')` 等）：

| 位置 | 数量 | 处理 |
|---|---|---|
| `test/cases/modules/onnx/`（含 `refusals/`） | 约 177 | `get_env('X')` → `get_env('X') ?? ''`；这批用例是机械替换，建议脚本一次改完。更好的做法：用例本来就要求变量存在，改成 `get_env('X') ?? ''` 之外不要加别的分支 |
| `test/cases/modules/tiles/` | 22 | 同上 |
| `test/cases/compile/native/`、`test/cases/opt/generic/`、`test/cases/modules/`、`test/cases/modules/std/env/` | 约 43 | 同上 |
| `docs/`（教程、参考） | 13 | 例子改为 `?? ''` 或 `match` |
| `get_cwd()` → 处理 `!`（8 处） | `test/cases/modules/std/env/{basic.cml,unicode/env_paths.cml,unicode/missing_file.cml}`、`test/cases/compile/native/env_declaration_import.cml`、`test/cases/modules/rename/env_{identity,new}.cml`、`test/cases/tools/unicode/unicode_paths.test.mjs` |
| 去掉 `module env` / 清单 | `test/plans/feat/compile/native_declarations.plan.toml`、`test/plans/feat/modules/{stdlib_registry,unicode_paths}.plan.toml`、`docs/language/05_modules.md` 与 `en/`、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/technical/41_resources_and_definitions.md` |

## 开放问题

1. **`env` 与 `process` 是否合并**。两者都是"进程所处的环境"，合并后 `get_env` 要换导入名（约 196 处，全是机械改）。推荐**不合并**：资源本就分开（`env` 与 `process`，3.3），模块分层表（conventions 6）也把它们并列在"系统"层。
2. **`get_env` 是否带默认值**：`get_env('X') ?? 'default'` 已经够短，不加 `with <fallback>`。
