# Camel Debugger (camel-db)

交互式 Camel 调试器：支持 REPL 与 Web UI 双通道控制，可查看 GC 内存布局、按 region 查看十六进制/对象视图，并支持**按类型的调试断点**（如「每分配暂停」）。断点类型由 libcamel 通用 API 统一管理，可在 Web UI 中按类型勾选启用/禁用；NDEBUG 构建下零开销，且无全局/静态初始化以保持延迟链接友好。

**开发与 Agent 必读**：产品核心定位与不可违反的设计原则见 [PRODUCT_PRINCIPLES.md](PRODUCT_PRINCIPLES.md)。修改 debugger 相关代码前请遵循该文档，避免引入与架构冲突的实现。

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
| `--run` | `-r` | 启动后自动 run 一次（需已通过 `-f` 或位置参数加载文件）。 |
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
| `run` | `r` | 运行已加载的文件；内存扫描与断点（GIR 节点、alloc 空间）由统一断点机制控制。 |
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

前端为 **Vue 3 + Vite** 项目，位于 `tools/debugger/ui/`。

- **生产**：先构建再由 Python 提供静态资源与 API 代理。
  ```bash
  cd tools/debugger/ui && npm install && npm run build
  # 在项目根或 tools/debugger 目录下
  python tools/debugger/serve_ui.py
  ```
  `serve_ui.py` 会优先使用 `ui/dist`（若存在且含 `index.html`），否则使用 `ui/`。

- **开发**：使用 Vite 开发服务器（代理 `/api` 到 camel-db）。
  ```bash
  cd tools/debugger/ui && npm install && npm run dev
  ```
  浏览器打开 Vite 提示的地址（如 http://127.0.0.1:5173）；需先在同一机启动 camel-db 并执行 `serve`。

默认在 http://127.0.0.1:8080 提供页面。若 API 端口不是 8765，需指定：

```bash
python tools/debugger/serve_ui.py --api-url http://127.0.0.1:9000
```

**步骤三：在浏览器中操作**

打开 http://127.0.0.1:8080，可：

- **文件路径**：输入要执行的 .cml 绝对路径或相对路径（相对当前工作目录）。
- **Load file**：仅加载文件，不运行。
- **Run**：先按当前路径加载（若未加载），再执行。运行期间内存扫描自动开启，下方展示内存布局与 region 列表。
- **断点**：统一断点模型，通过一套接口与数据结构表示和管理；按类型配置（如节点型在 GIR 图双击设置、分配空间型通过 REPL/API 的 `alloc-breakpoint-spaces` 设置）。命中时暂停，点 **Continue** 继续；Run/Restart 时父进程将当前断点状态推送给任务。
- **GIR 图**：Run 后点 Refresh 加载 GIR；点击子图节点可展开，Back 返回上一级。**双击**普通节点可设置/取消节点型断点（红框）；执行到该节点时暂停（需使用 NodeVM 调度器），暂停节点高亮显示。
- 点击任意 **region 行**可打开弹层，切换 **Hex view**（十六进制分页）或 **Object view**（对象列表分页）。
- **Continue**：断点暂停时继续执行；**Restart**：从当前脚本重新跑一遍（会清空当前 snapshot/Last alloc，显示「Restarting…」直至再次暂停后自动刷新视图）。
- **Verbose log** 分为两个 tab：**Debugger**（主进程 REPL/命令回显）、**Task**（当前任务子进程的 log，含该任务内执行的命令回显与脚本输出）。勾选 Verbose 后，对应当前任务的设置，回显出现在 **Task** tab。
- 侧栏 **Tasks** 列出当前运行中的任务（如 `hello.cml running`），点击可切换当前任务；内存/断点/Continue 等操作均针对当前选中任务。

状态行会显示 API 是否连接、当前加载文件、是否在扫描内存等。

## 任务与 target（多进程下的请求目标）

- **GET /api/state** 仅返回**父进程全局状态**（含 `tasks` 列表及每个任务的聚合字段 paused、pauseReason、lastAlloc、assertionError 等），**不接受 target**，不转发。
- **GET /api/task-state?target=&lt;任务id&gt;** 返回**单个任务**（worker）的 state，与全局 state 结构不同（无 tasks 列表）；需获取某任务原始 state 时使用。
- 其余与「当前运行任务」相关的接口（snapshot、region、breakpoint-types/spaces、settings、log、gir-json、gir-breakpoints、continue、restart、terminate）支持查询参数或 Body 中的 **`target`**（任务 id）。任务是否暂停、last-alloc、assertionError 通过 GET /api/state 的 tasks[] 聚合字段获取。
- 请求带 `target` 时，父进程将请求**转发**到该任务对应的子进程；未带 `target` 且当前仅有一个任务时，父进程也会转发到该唯一任务。
- **父进程在有任务时，不会用本地数据应答上述按任务接口**：若存在多个任务且未指定 `target`，会返回 400，提示在 UI 中选择任务。

## API 概览（供二次开发）

完整请求/响应与 target 约定见 [docs/API.md](docs/API.md)。以下为快速索引。

camel-db 在 `serve` 后提供以下 HTTP 接口（默认 `http://127.0.0.1:8765`）。凡「按任务」的接口均可加 `?target=<任务id>`（GET）或 Body `"target": "<任务id>"`（POST），父进程会转发到对应子进程。

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/state` | 仅全局状态：serverRunning、hasFile、targetFile、**tasks**（每项含 id、port、scriptPath、taskState 及聚合字段 paused、pauseReason、lastAlloc、assertionError）。不接受 target，不转发。 |
| GET | `/api/task-state` | 单任务状态：必须 `?target=<任务id>`，转发到该任务并返回其 state（与全局 state 结构不同，无 tasks 列表）。 |
| GET | `/api/settings` | 当前进程/任务的设置：`{ verbose, logFile, allocStepEnabled, allocBreakpointsAvailable, memoryMonitorRunning }`。支持 `?target=` 转发到任务。 |
| POST | `/api/file` | Body: `{"path": "..."}`，加载文件。 |
| POST | `/api/run` | Body 可选 `{"target": "<任务id>"}`；spawn 并执行 run。内存扫描与断点按统一模型处理，新 worker 启动后父进程会推送当前断点状态（breakpoint-spaces、gir-breakpoints 等）。 |
| GET | `/api/breakpoints` | 按类型返回断点状态：`{ "alloc": { "spaces": ["perm", "meta", ...] }, ... }`。支持 `?target=`。 |
| GET | `/api/breakpoint-types` | 断点类型：`{"known": ["alloc", ...], "enabled": ["alloc", ...]}`。支持 `?target=`。 |
| POST | `/api/breakpoint-types` | Body: `{"enabled": ["alloc", ...]}`，设置启用的断点类型。支持 Body `target`。 |
| GET | `/api/breakpoint-spaces` | 当前断点配置（分配空间型）。支持 `?target=`。空 = 该类型未设过滤。 |
| POST | `/api/breakpoint-spaces` | Body: `{"breakSpaces": [...]}`，设置断点（分配空间型）；操作时即同步到对应 task。支持 Body `target`。 |
| POST | `/api/settings` | Body: `{"verbose": true, "logFile": "path"}`；`logFile` 为空则关闭日志文件。支持 Body `target` 转发到任务。 |
| GET | `/api/snapshot` | 当前 GC 内存快照 JSON；响应含 `source`: `"worker"`|`"parent"` 表示数据来源。支持 `?target=`。 |
| GET | `/api/log` | 日志行：`{"lines": string[], "nextOffset": number}`。**无 target** 为父进程 log（Debugger tab）；**带 `?target=`** 为对应任务 log（Task tab）。 |
| POST | `/api/continue` | 断点暂停时继续。支持 Body `target`。 |
| POST | `/api/restart` | 从当前脚本重新跑。支持 Body `target`。 |
| POST | `/api/terminate` | 请求终止任务。支持 Body `target`。 |
| GET | `/api/region/:name/memory?offset=&limit=` | 指定 region 的原始内存字节。支持 `?target=`。 |
| GET | `/api/region/:name/objects?offset=&limit=` | 指定 region 的对象列表分页。支持 `?target=`。 |
| GET | `/api/gir-json` | GIR 图 JSON（懒加载）。无 `?graphId=` 返回根图摘要；有 `?graphId=` 返回该图 nodes/edges。支持 `?target=`、`?path=`。 |
| GET | `/api/gir-breakpoints` | 当前断点配置（节点型）：`{ "nodeIds": ["0x...", ...] }`。支持 `?target=`。 |
| POST | `/api/gir-breakpoints` | Body: `{"nodeIds": ["0x...", ...]}`，设置断点（节点型，全量替换）。支持 Body `target`。 |

前端通过 `serve_ui.py` 代理访问上述 API，因此页面中请求为相对路径（如 `/api/state`）即可。

### Web UI 状态与存储约定

- **业务状态**（当前文件、任务列表、断点配置、任务设置等）**仅来自后端**，前端不持久化；刷新或重开页面后由 API 与当前任务决定展示内容。
- **UI 布局偏好**可使用浏览器 sessionStorage 持久化，以便刷新后保持体验，包括：主 Tab（Run/Memory/GIR）、侧栏折叠、日志面板高度与折叠。键名见 `ui/js/app.js`（`UI_STORAGE`）与 `ui/js/monitor.js`（`LOG_PANEL_STORAGE_KEY`）。

## 扩展：在 libcamel 中埋点（按类型断点）

断点由 **libcamel 通用 API** 统一管理，头文件：`include/camel/core/debug_breakpoint.h`。

- **注册类型**：在模块中调用 `camel::DebugBreakpoint::RegisterType("类型名")`（如 `"alloc"`、`"gc"`），便于 Web UI 列出。
- **埋点**：在需要暂停的位置调用 `camel::DebugBreakpoint::Hit("类型名", &context)`；若该类型已启用且已设置 handler，则会调用 handler（debugger 中会在此暂停并等待 Continue）。
- **特性**：状态使用函数内 static 懒加载，**无全局/静态初始化**，不影响延迟链接；**NDEBUG 下 `Hit()` 为头文件内联空实现，零开销**。

在内存分配路径中已埋点：**分配前** `Hit("alloc_before", &preEvt)`、**分配后** `Hit("alloc", &evt)`（见 `include/camel/core/mm/debug_hook.h` 与 `src/core/mm/debug_hook.cpp`）。勾选「Memory alloc」时两者均启用并会先后暂停。若要在 GC 或其他模块增加新断点类型，只需调用 `RegisterType("gc")` 并在合适位置调用 `Hit("gc", &context)`，Web UI 的「Breakpoint types」会自动列出并可勾选。

## 多进程架构（默认且唯一）

Run **始终**在子进程中执行：父进程只做 API 与转发，子进程自身起 HTTP 服务，与父进程通过 HTTP 通信。

- **父进程**：常驻 API（如 8765），处理 `/api/file`、`/api/settings`、`/api/state` 等；收到 `/api/run` 时 spawn 子进程（传入 `CAMEL_DB_WORKER_PORT=8766` 等），并将按任务请求（snapshot、settings、log、continue、restart、terminate、gir-json、断点相关 API、region/* 等）**按 target 转发**到对应子进程；父进程保留断点状态，Run/Restart 时推送给 worker；子进程退出后从任务列表清除。
- **子进程**：`camel-db --run-worker <path>`；读取 `CAMEL_DB_WORKER_PORT` 后在本机该端口起 HTTP 服务（仅 127.0.0.1），执行脚本；解释器错误等仍写 stderr，与协议无关；脚本结束后进程退出。子进程内执行的命令（如通过 Web UI 设置的 verbose）会回显到该进程的 log，在 Web UI 的 **Task** tab 中查看。

**子进程清理**：主进程退出时（正常 return 或 atexit）会调用 `terminateAllWorkers()` 终止子进程；Windows 上还会使用 **Job Object**（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`），子进程被加入该 Job，主进程异常退出时系统会自动关闭 Job 并终止其内所有进程，避免残留子进程。

这样 **协议即现有 HTTP API**，无需管道或自定义协议；Memory monitor、Pause on alloc、Continue/Restart/Terminate 均可用。当前仅 Windows 实现了 spawn（`CreateProcess` + Job Object）；Unix 需补全 `fork`/`exec` 并设置 `CAMEL_DB_WORKER_*` 环境变量。

更详细的架构与“父进程=仅网关、子进程=运行时”的重构说明见 [REFACTOR_PLAN.md](REFACTOR_PLAN.md)。

## 故障排除

- **仅有已退出任务时显示「No task」**：已修复。当所有任务均为 exited 时，前端仍会保留当前任务并显示「Task: Exited」，可点击 Run 重新运行；`/api/settings` 与 `/api/log` 对已退出任务会返回父进程设置或该任务的缓存日志，不再返回 400。
- **开发时出现 504 (Outdated Optimize Dep)**：Vite 预构建缓存过期。在 `tools/debugger/ui/` 下删除 `node_modules/.vite` 后重新执行 `npm run dev`，或执行一次 `npm run dev:force` 强制重新预构建。

## 目录说明

```
tools/debugger/
├── README.md              # 本说明
├── PRODUCT_PRINCIPLES.md  # 产品核心定位与设计原则（开发/Agent 必读）
├── REFACTOR_PLAN.md       # 架构重构方案（若已落地；父=网关 / 子=运行时）
├── CMakeLists.txt     # 构建配置
├── main.cpp           # 入口、命令行解析、atexit 清理、回调注册与启动
├── server.h/cpp       # API 服务（serve）+ 内存扫描、按 target 转发、requireNoTasksForLocal
├── src/
│   ├── state.h/cpp    # 全局状态、getStateJson、getLogFilePath、tasks、resolveTargetToPort
│   ├── compile.h/cpp  # 编译上下文、createCompilationStateForPath、getGirJson
│   ├── gir_json.h/cpp # GIR 懒加载 JSON 序列化（根图摘要、按 graphId 展开）
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
