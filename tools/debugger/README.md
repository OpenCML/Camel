# Camel Debugger (camel-db)

交互式 Camel 调试器：支持 REPL 与 Web UI 双通道控制，可查看 GC 内存布局、按 region 查看十六进制/对象视图，并支持**按类型的调试断点**（如「每分配暂停」）。断点类型由 libcamel 通用 API 统一管理，可在 Web UI 中按类型勾选启用/禁用；NDEBUG 构建下零开销，且无全局/静态初始化以保持延迟链接友好。

## 构建

在项目根目录：

```bash
cd build
cmake --build . --config Release --target camel-debugger
```

产物：`build/tools/debugger/Release/camel-db.exe`（Windows）或对应目录下的 `camel-db`。

**若需使用「每分配暂停」等断点**：请使用 **Debug** 构建（`--config Debug`），否则 libcamel 内分配路径的 hook 会被编译掉，perm/Type 等分配不会触发断点。

## 使用方式

### 1. 启动 debugger

```bash
# 从项目根或 build 目录运行
./build/tools/debugger/Release/camel-db

# 直接指定要加载的源文件（位置参数）
./build/tools/debugger/Release/camel-db path/to/script.cml
```

进入 REPL 后提示符为 `(camel-db)`。

**启动参数（可与位置参数组合）**：

| 参数 | 简写 | 说明 |
|------|------|------|
| `--serve [port]` | `-s` | 启动时即开启 API 服务（默认 8765）。 |
| `--file <path>` | `-f` | 启动时加载要调试的文件。 |
| `--run` | `-r` | 启动后自动 run 一次（需已通过 `-f` 或位置参数加载文件；默认 memory monitor 开、alloc-step 关）。 |
| `--verbose` | `-V` | 启动即开启详细输出。 |
| `--logfile <path>` | | 启动即把程序输出写入指定文件。 |
| `--run-worker <path>` | | 子进程入口：启动后在本机端口（由环境变量 `CAMEL_DB_WORKER_PORT` 指定）起 HTTP 服务，执行脚本后退出；供父进程 spawn 使用，用户一般不直接调用。 |
| `--help` | `-h` | 显示帮助并退出。 |
| `--version` | `-v` | 显示版本并退出。 |

示例：

```bash
camel-db -s
camel-db --serve 9000 hello.cml
camel-db hello.cml -s -r
camel-db -f script.cml --serve --logfile debug.log
```

### 2. REPL 命令

| 命令 | 简写 | 说明 |
|------|------|------|
| `serve [port]` | `s` | 启动 API 服务（默认 8765）。**需先执行 serve，Web UI 和 API 才能用**。 |
| `file <path>` | `f` | 加载要调试的 .cml 文件。 |
| `run` | `r` | 运行已加载的文件（默认开启内存监控，不开启 alloc-step）。 |
| `verbose [on\|off]` | | 开启/关闭详细输出。 |
| `logfile [path]` | | 将程序输出写入文件（path 为空或 off 则关闭）；与 Logger 共用，可动态追加。 |
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

### 3. Web UI（推荐）

API 服务与前端分离：camel-db 只提供 HTTP API，页面由 Python 脚本单独提供。

**步骤一：在 REPL 中启动 API**

```
(camel-db) serve
```

默认端口 8765；可指定端口：`serve 9000`。

**步骤二：启动前端**

在**另一个终端**执行（需先安装 Python）：

```bash
# 在项目根或 tools/debugger 目录下
python tools/debugger/serve_ui.py
```

默认在 http://127.0.0.1:8080 提供页面。若 API 端口不是 8765，需指定：

```bash
python tools/debugger/serve_ui.py --api-url http://127.0.0.1:9000
```

**步骤三：在浏览器中操作**

打开 http://127.0.0.1:8080，可：

- **文件路径**：输入要执行的 .cml 绝对路径或相对路径（相对当前工作目录）。
- **Load file**：仅加载文件，不运行。
- **Run**：先按当前路径加载（若未加载），再执行。运行前可勾选：
  - **Enable memory monitor**：开启 GC 区域扫描，下方展示内存布局与 region 列表。
- **Breakpoint types**：按类型启用调试断点（Debug 构建下有效）。勾选 **Memory alloc** 时，会在**分配前**与**分配后**两处均暂停（均需点「Continue」继续）；页面会显示本次暂停是「Paused before alloc」还是「Paused after alloc」及 size/space 等说明。
- 点击任意 **region 行**可打开弹层，切换 **Hex view**（十六进制分页）或 **Object view**（对象列表分页）。
- **Continue**：alloc-step 暂停时继续执行；**Restart**：从当前脚本重新跑一遍（仅在暂停时可用；会清空当前 snapshot/Last alloc，显示「Restarting…」直至再次暂停后自动刷新视图）。
- **Verbose log** 分为两个 tab：**Debugger**（主进程 REPL/命令回显）、**Task**（当前任务子进程的 log，含该任务内执行的命令回显与脚本输出）。勾选 Verbose 后，对应当前任务的设置，回显出现在 **Task** tab。
- 侧栏 **Tasks** 列出当前运行中的任务（如 `hello.cml running`），点击可切换当前任务；内存/断点/Continue 等操作均针对当前选中任务。

状态行会显示 API 是否连接、当前加载文件、是否在扫描内存等。

## 任务与 target（多进程下的请求目标）

- **GET /api/state** 始终返回**父进程**状态（含 `tasks` 列表），不转发。**verbose、allocStepEnabled、allocBreakpointsAvailable、taskState** 均为任务级：父进程在有任务时不再在顶层返回这些字段；前端从当前选中任务的 `tasks[].taskState` 与 **GET /api/settings?target=** 获取该任务的设置与能力。
- 其余与「当前运行任务」相关的接口（snapshot、region、step-paused、last-alloc、breakpoint-types/spaces、settings、log、gir-dot、continue、restart、terminate）支持查询参数或 Body 中的 **`target`**（任务 id，一般为子进程端口如 `8766`）。
- 请求带 `target` 时，父进程将请求**转发**到该任务对应的子进程；未带 `target` 且当前仅有一个任务时，父进程也会转发到该唯一任务。
- **父进程在有任务时，不会用本地数据应答上述按任务接口**：若存在多个任务且未指定 `target`，会返回 400，提示在 UI 中选择任务。

## API 概览（供二次开发）

camel-db 在 `serve` 后提供以下 HTTP 接口（默认 `http://127.0.0.1:8765`）。凡「按任务」的接口均可加 `?target=<任务id>`（GET）或 Body `"target": "<任务id>"`（POST），父进程会转发到对应子进程。

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/state` | 父进程状态：serverRunning、memoryMonitorRunning、hasFile、targetFile、**tasks**（任务列表，每项含 id、port、scriptPath、**taskState**）。父进程**有任务时**不再返回顶层 verbose/allocStepEnabled/allocBreakpointsAvailable/taskState，这些为**任务级**，由 GET /api/settings?target= 与 tasks[].taskState 提供。不转发。 |
| GET | `/api/settings` | 当前进程/任务的设置：`{ verbose, logFile, allocStepEnabled, allocBreakpointsAvailable, memoryMonitorRunning }`。支持 `?target=` 转发到任务。 |
| POST | `/api/file` | Body: `{"path": "..."}`，加载文件。 |
| POST | `/api/run` | Body: `{"memoryMonitor": true, "allocStep": false, "breakSpaces": [...]}`，spawn 并执行 run；父进程会将 run 参数转发给新子进程。 |
| GET | `/api/breakpoint-types` | 断点类型：`{"known": ["alloc", ...], "enabled": ["alloc", ...]}`。支持 `?target=`。 |
| POST | `/api/breakpoint-types` | Body: `{"enabled": ["alloc", ...]}`，设置启用的断点类型。支持 Body `target`。 |
| GET | `/api/breakpoint-spaces` | 当前启用的断点空间（alloc 过滤）。支持 `?target=`。 |
| POST | `/api/breakpoint-spaces` | Body: `{"breakSpaces": [...]}`。支持 Body `target`。 |
| POST | `/api/settings` | Body: `{"verbose": true, "logFile": "path"}`；`logFile` 为空则关闭日志文件。支持 Body `target` 转发到任务。 |
| GET | `/api/snapshot` | 当前 GC 内存快照 JSON；响应含 `source`: `"worker"`|`"parent"` 表示数据来源。支持 `?target=`。 |
| GET | `/api/step-paused` | 是否暂停及本次原因：`{ "paused": true, "phase": "before"|"after", "size", "space"[, "ptr"] }`。支持 `?target=`。 |
| GET | `/api/last-alloc` | 最近一次断点信息。支持 `?target=`。 |
| GET | `/api/log` | 日志行：`{"lines": string[], "nextOffset": number}`。**无 target** 为父进程 log（Debugger tab）；**带 `?target=`** 为对应任务 log（Task tab）。 |
| POST | `/api/continue` | 断点暂停时继续。支持 Body `target`。 |
| POST | `/api/restart` | 从当前脚本重新跑。支持 Body `target`。 |
| POST | `/api/terminate` | 请求终止任务。支持 Body `target`。 |
| GET | `/api/region/:name/memory?offset=&limit=` | 指定 region 的原始内存字节。支持 `?target=`。 |
| GET | `/api/region/:name/objects?offset=&limit=` | 指定 region 的对象列表分页。支持 `?target=`。 |
| GET | `/api/gir-dot` | GIR 图 DOT。支持 `?target=`、`?path=`。 |

前端通过 `serve_ui.py` 代理访问上述 API，因此页面中请求为相对路径（如 `/api/state`）即可。

## 扩展：在 libcamel 中埋点（按类型断点）

断点由 **libcamel 通用 API** 统一管理，头文件：`include/camel/core/debug_breakpoint.h`。

- **注册类型**：在模块中调用 `camel::DebugBreakpoint::RegisterType("类型名")`（如 `"alloc"`、`"gc"`），便于 Web UI 列出。
- **埋点**：在需要暂停的位置调用 `camel::DebugBreakpoint::Hit("类型名", &context)`；若该类型已启用且已设置 handler，则会调用 handler（debugger 中会在此暂停并等待 Continue）。
- **特性**：状态使用函数内 static 懒加载，**无全局/静态初始化**，不影响延迟链接；**NDEBUG 下 `Hit()` 为头文件内联空实现，零开销**。

在内存分配路径中已埋点：**分配前** `Hit("alloc_before", &preEvt)`、**分配后** `Hit("alloc", &evt)`（见 `include/camel/core/mm/debug_hook.h` 与 `src/core/mm/debug_hook.cpp`）。勾选「Memory alloc」时两者均启用并会先后暂停。若要在 GC 或其他模块增加新断点类型，只需调用 `RegisterType("gc")` 并在合适位置调用 `Hit("gc", &context)`，Web UI 的「Breakpoint types」会自动列出并可勾选。

## 多进程架构（默认且唯一）

Run **始终**在子进程中执行：父进程只做 API 与转发，子进程自身起 HTTP 服务，与父进程通过 HTTP 通信。

- **父进程**：常驻 API（如 8765），处理 `/api/file`、`/api/settings`、`/api/state` 等；收到 `/api/run` 时 spawn 子进程（传入 `CAMEL_DB_WORKER_PORT=8766` 等），并将按任务请求（`/api/snapshot`、`/api/settings`、`/api/log`、`/api/continue`、`/api/restart`、`/api/terminate`、`/api/gir-dot`、`/api/region/*`、breakpoint-types/spaces 等）**按 target 转发**到对应子进程；子进程退出后从任务列表清除。
- **子进程**：`camel-db --run-worker <path>`；读取 `CAMEL_DB_WORKER_PORT` 后在本机该端口起 HTTP 服务（仅 127.0.0.1），执行脚本；解释器错误等仍写 stderr，与协议无关；脚本结束后进程退出。子进程内执行的命令（如通过 Web UI 设置的 verbose）会回显到该进程的 log，在 Web UI 的 **Task** tab 中查看。

**子进程清理**：主进程退出时（正常 return 或 atexit）会调用 `terminateAllWorkers()` 终止子进程；Windows 上还会使用 **Job Object**（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`），子进程被加入该 Job，主进程异常退出时系统会自动关闭 Job 并终止其内所有进程，避免残留子进程。

这样 **协议即现有 HTTP API**，无需管道或自定义协议；Memory monitor、Pause on alloc、Continue/Restart/Terminate 均可用。当前仅 Windows 实现了 spawn（`CreateProcess` + Job Object）；Unix 需补全 `fork`/`exec` 并设置 `CAMEL_DB_WORKER_*` 环境变量。

更详细的架构与“父进程=仅网关、子进程=运行时”的重构说明见 [REFACTOR_PLAN.md](REFACTOR_PLAN.md)。

## 目录说明

```
tools/debugger/
├── README.md          # 本说明
├── REFACTOR_PLAN.md   # 架构重构方案（父=网关 / 子=运行时）
├── CMakeLists.txt     # 构建配置
├── main.cpp           # 入口、命令行解析、atexit 清理、回调注册与启动
├── server.h/cpp       # API 服务（serve）+ 内存扫描、按 target 转发、requireNoTasksForLocal
├── src/
│   ├── state.h/cpp    # 全局状态、getStateJson、getLogFilePath、tasks、resolveTargetToPort
│   ├── compile.h/cpp  # 编译上下文、createCompilationStateForPath、getGirDot
│   ├── spawn.h/cpp    # 子进程 spawn（Windows CreateProcess + Job Object）、terminateAllWorkers
│   ├── run.h/cpp      # RunOutcome、clearRunState、runScriptOnce
│   ├── worker.h/cpp   # 子进程入口 runWorkerMode（--run-worker）、post-execute 回显
│   ├── repl.h/cpp     # 帮助/版本/横幅、parseCommand、REPL 主循环
│   └── command/       # 命令模式：dispatcher、commands（configure、setBreakpointFilter 等）
├── serve_ui.py        # 前端静态服务 + /api 代理到 camel-db
├── ui/
│   ├── index.html     # Web UI 单页（Tasks、Run、内存与 region 视图、Log 双 tab）
│   ├── js/            # app、api、monitor、hex、graph 等
│   └── css/style.css  # 样式
└── build_config.h.in  # 构建信息
```
