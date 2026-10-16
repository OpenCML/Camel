# Camel Debugger (camel-db)

`camel-db` 提供原生 DAP、REPL 和本机 HTTP 后端。维护的图形界面位于
[OpenCML-Ext](https://github.com/OpenCML/OpenCML-Ext)，独立浏览器前端已迁移移除。
开发前请阅读 [PRODUCT_PRINCIPLES.md](PRODUCT_PRINCIPLES.md)。

## 构建

在项目根目录运行 `npm run build`（Release）或 `npm run debug`（Debug），标准脚本
将工具、动态库和 stdlib 收集到 `out/latest/`。分配断点依赖 Debug 构建；普通源码调试、
编译边界暂停和内存检查也支持 Release。不要覆盖正在使用的运行时。

## VS Code 调试

安装 OpenCML-Ext 并配置 Camel 运行时。插件直接启动 `camel-db --dap`，不需要先执行
`--serve`，也不需要浏览器或 Python 代理。示例 `launch.json` 配置：

```json
{
  "type": "camel",
  "request": "launch",
  "name": "Inspect compiler passes",
  "program": "${file}",
  "passes": ["std::macro", "std::inline", "std::nvm"],
  "stopOnPassEntry": true,
  "passBreakpoints": [2]
}
```

边界 0 是执行所有 pass 前；后续编号是某遍返回 GIR 后的边界，重复 pass 名称仍有
独立编号。消费图的调度/导出遍不产生遍后 GIR，不能伪造一个快照。
GIR 自动显示实际暂停位置；“下一遍”推进执行，历史快照下拉框只切换查看对象。
源码单步、断点、调用栈、变量、异常、会话切换和重启使用 VS Code 原生调试视图。

命令 **Camel：查看调试内存**（**Camel: Inspect Debug Memory**）或 GIR 调试栏的内存
图标打开所选会话的区域概览和字节/对象分页。仅在暂停时读取；继续执行、切换会话
或到达另一暂停位置会清除旧结果。显式刷新可获取新快照，不连续轮询整个堆。
分配断点显示构建支持状态及暂停前的空间和大小；原生算子输出、诊断和内部断言
进入 Debug Console，错误不会被一个遮挡界面的弹层吞掉。

## 终端 REPL

```sh
camel-db path/to/program.cml
camel-db --serve 8765
```

`--dap` 专用于编辑器的标准输入输出协议。`--serve [port]`（`-s`）为终端/自动化开启
本机 API；`--file`（`-f`）加载文件，`--run`（`-r`）启动执行，`--verbose`（`-V`）开启
详细日志，`--logfile` 指定日志文件。`--run-worker` 是内部入口，不供用户直接调用。

| 命令 | 简写 | 说明 |
|------|------|------|
| `serve [port]` | `s` | 启动 API 服务（默认 8765）。**HTTP 客户端需先执行 serve；DAP 无需此步骤**。 |
| `file <path>` | `f` | 加载要调试的 .cml 文件。 |
| `run` | `r` | 运行已加载的文件；内存扫描与断点（GIR 节点、alloc 空间）由统一断点机制控制。 |
| `verbose [on\|off]` | | 开启/关闭详细输出（同上：对应库内 **info** / **fatal** 阈值；与 `camel` CLI 的 `-v`/`-l` 语义一致，均为 `Logger` 全局阈值）。 |
| `logfile [path]` | | 将程序输出写入文件（path 为空或 off 则关闭）；与 Logger 共用，可动态追加。 |
| `break <文件>:<行>` / `break <行>` / `break <函数>` | `b` | 源码行断点，或按完整名字（`模块::路径::名字`，只有一个模块有该名字时可省略模块）的函数断点。 |
| `delete` | | 删除全部源码行断点与函数断点。 |
| `run [遍 ...]` | `r` | 运行（可带遍序列，如 `run std::fvm`）并等到停下或结束；途中打印程序输出。 |
| `continue` | `c` | 继续并等到下一次停下。 |
| `next` / `step` / `finish` | `n` | 按源码行（执行顺序）单步跳过 / 进入 / 跳出；越过的、因无人使用其结果而未执行的行会给出说明。 |
| `nexti` / `stepi` | `ni` / `si` | 按图节点单步跳过 / 进入。 |
| `pause` | | 让运行中的程序在下一个节点停下。 |
| `bt` | `backtrace` / `where` | 调用栈（完整名字）。 |
| `locals [n]` | | `bt` 第 n 帧的变量（默认最上面一帧）。 |
| `print <名字>` | `p` | 名字的值，可带成员访问。 |
| `frames [functions\|subgraphs]` | | 分支的臂并入所在函数的帧（默认），或显示真实的子图结构。 |
| `help` | `h` / `?` | 显示帮助。 |
| `version` | `v` | 显示版本。 |
| `quit` | `q` / `exit` | 退出 debugger（会停止 API 服务）。 |

示例：

```
(camel-db) serve
(camel-db) file hello.cml
(camel-db) run
(camel-db) quit
```

源码级调试的命令（断点、单步、栈、变量、`frames`）作用于前台任务，经父进程的 `debug` 命令转发给该任务的调试核心；
一次完整的会话见 [docs/debugging.cn.md](../../docs/debugging.cn.md) 第 2 节。


## 后端接口与职责

- LSP 负责源码分析和编辑器能力；它不控制调试执行。
- 原生 DAP 为每个 VS Code 会话管理独立 worker，使用 debug core 的真实暂停状态。
- REPL/HTTP 网关使用显式任务 target；终端可选择前台任务。执行始终在 worker 内。
- worker 内部控制仍使用本机 HTTP；插件不发现端口、不选择“第一个任务”。
- DebugBreakpoint 的分配钩子保留给 Debug 构建与 REPL；源码、节点、异常和编译边界
  暂停由 debug core 提供，不能全部描述成 NDEBUG 下消失的类型钩子。
- stdout/stderr、调试诊断和生命周期由 DAP 转发；停止、重启和退出清理 worker。

HTTP 请求、响应和 target 约定见 [docs/API.md](docs/API.md)。源码级调试教程见
[docs/debugging.cn.md](../../docs/debugging.cn.md)，DAP、帧、异常和编译流水线设计见
[docs/technical/35_debugger.md](../../docs/technical/35_debugger.md)。

## 代码位置

- `src/core/`：断点、单步、帧、变量、异常、编译边界及暂停线程内存检查。
- `src/dap/`：标准输入输出 DAP 与会话 worker 生命周期。
- `src/command/`、`src/repl.cpp`：终端与网关命令分发。
- `server.cpp`、`src/worker.cpp`、`src/spawn.cpp`：本机 HTTP、worker 与进程清理。
- `test/cases/tools/debugger/`：REPL、HTTP 和 DAP 协议回归测试。

图形前端、交互和中英文界面由插件仓库维护；本仓库不再包含 Python 静态代理、
独立 Vue 项目或嵌入页面的生成脚本。
