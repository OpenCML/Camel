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

状态行会显示 API 是否连接、当前加载文件、是否在扫描内存等。

## API 概览（供二次开发）

camel-db 在 `serve` 后提供以下 HTTP 接口（默认 `http://127.0.0.1:8765`）。

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/state` | 当前状态：serverRunning、memoryMonitorRunning、allocStepEnabled、hasFile、targetFile。 |
| POST | `/api/file` | Body: `{"path": "..."}`，加载文件。 |
| POST | `/api/run` | Body: `{"memoryMonitor": true, "allocStep": false}`，按选项执行 run；`allocStep` 与「Breakpoint types」中 alloc 是否勾选一致。 |
| GET | `/api/breakpoint-types` | 断点类型：`{"known": ["alloc", ...], "enabled": ["alloc", ...]}`。 |
| POST | `/api/breakpoint-types` | Body: `{"enabled": ["alloc", ...]}`，设置当前启用的断点类型。 |
| POST | `/api/settings` | Body: `{"verbose": true, "logFile": "path"}`；`logFile` 为空则关闭日志文件，否则程序输出（含 Logger 与 debugger 消息）追加写入该文件并实时 flush。 |
| GET | `/api/snapshot` | 当前 GC 内存快照 JSON（需已通过 run 开启内存监控）。 |
| GET | `/api/step-paused` | 是否暂停及本次原因：`{ "paused": true, "phase": "before"|"after", "size", "space"[, "ptr"] }`。 |
| GET | `/api/last-alloc` | 最近一次断点信息：`phase`（before/after）、`size`、`space`，分配后还有 `ptr`。 |
| POST | `/api/continue` | 断点暂停时，继续执行。 |
| GET | `/api/region/:name/memory?offset=&limit=` | 指定 region 的原始内存字节（JSON：`capacity`、`used`、`data` 字节数组）；hex/ASCII 格式化与分页由 Web UI 完成。 |
| GET | `/api/region/:name/objects?offset=&limit=` | 指定 region 的对象列表分页。 |

前端通过 `serve_ui.py` 代理访问上述 API，因此页面中请求为相对路径（如 `/api/state`）即可。

## 扩展：在 libcamel 中埋点（按类型断点）

断点由 **libcamel 通用 API** 统一管理，头文件：`include/camel/core/debug_breakpoint.h`。

- **注册类型**：在模块中调用 `camel::DebugBreakpoint::RegisterType("类型名")`（如 `"alloc"`、`"gc"`），便于 Web UI 列出。
- **埋点**：在需要暂停的位置调用 `camel::DebugBreakpoint::Hit("类型名", &context)`；若该类型已启用且已设置 handler，则会调用 handler（debugger 中会在此暂停并等待 Continue）。
- **特性**：状态使用函数内 static 懒加载，**无全局/静态初始化**，不影响延迟链接；**NDEBUG 下 `Hit()` 为头文件内联空实现，零开销**。

在内存分配路径中已埋点：**分配前** `Hit("alloc_before", &preEvt)`、**分配后** `Hit("alloc", &evt)`（见 `include/camel/core/mm/debug_hook.h` 与 `src/core/mm/debug_hook.cpp`）。勾选「Memory alloc」时两者均启用并会先后暂停。若要在 GC 或其他模块增加新断点类型，只需调用 `RegisterType("gc")` 并在合适位置调用 `Hit("gc", &context)`，Web UI 的「Breakpoint types」会自动列出并可勾选。

## 目录说明

```
tools/debugger/
├── README.md          # 本说明
├── CMakeLists.txt     # 构建配置
├── main.cpp           # 入口、REPL、与 debugger 逻辑
├── debugger_server.h/cpp  # Debugger API 服务（serve）+ 可选内存扫描
├── serve_ui.py        # 前端静态服务 + /api 代理到 camel-db
├── ui/
│   └── index.html    # Web UI 单页（选文件、选项、Run、内存与 region 视图）
└── build_config.h.in  # 构建信息
```
