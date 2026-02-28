# Camel Debugger HTTP API 规格

本文档为 camel-db 的 HTTP API 契约，供 Web UI、CLI 与后续开发/Agent 一致遵守。完整请求/响应与 target 约定以此为准。

---

## 1. 概述

- **Base URL**：默认 `http://127.0.0.1:8765`（可通过启动参数修改）。
- **Content-Type**：请求与响应均为 `application/json`。
- **target 通用约定**：
  - **任务 id**：即任务对应子进程监听的**端口号**（如 `"8766"`），与 path 无关；任务唯一由 id 区分。
  - **全局接口**：不使用 target，不转发（如 GET /api/state 无 target 时返回父进程状态）。
  - **按任务 GET**：查询参数 `?target=<任务id>` 可选。
  - **按任务 POST**：Body 中 `"target": "<任务id>"` 可选。
  - **无 target 时**：后端支持**前台任务**；存在前台时由服务端解析为前台任务。多任务且无 target 且无前台时返回 400。**Web UI 调用 API 时必须显式传 target**，不依赖前台机制。
  - 任务是否暂停、last-alloc 仅通过 GET /api/state 的聚合字段获得，父进程不暴露 GET /api/step-paused、GET /api/last-alloc。
- **打开文件即建任务**：POST /api/file 成功后父进程会 spawn 子进程并注册任务，任务状态为 `loaded`；同一文件可多次打开，每次新建 worker 与新任务。

---

## 2. 响应与错误

- **成功**：多数 GET 直接返回业务 JSON 对象；由 `dispatchAndRespond` 处理的 POST 返回 `{ "ok": true }` 或带业务体的响应；失败时返回 `{ "ok": false, "error": "<消息>" }`。
- **错误体**：统一为 `{ "ok": false, "error": "<消息>" }`（部分端点使用 `"error"` 键）。HTTP 状态码与错误码约定：
  - **400**：参数错误、缺少必选 target（多任务时）、任务不存在或已退出等。
  - **502**：仅当父进程作为网关请求子进程失败且**未**被转换为 200+默认时使用。父进程不暴露 step-paused、last-alloc，客户端仅通过 GET /api/state 获取任务暂停与 last-alloc 信息。

---

## 3. 端点列表

### 3.1 GET /api/state

- **target**：可选。有 target 时转发到子进程，返回该任务的完整状态（含 assertionError 等）；无 target 时返回父进程状态（含 tasks 列表）。
- **请求**：无 Body；可选查询参数 `target`。
- **响应**（无 target，父进程状态）：
  - `serverRunning`: boolean
  - `hasFile`: boolean
  - `targetFile`: string
  - `tasks`: array，每项 `{ "id", "port", "scriptPath", "taskState" }`；taskState 为 `"loaded"` | `"running"` | `"paused"` | `"completed"` | `"exited"` 等。
  - 当父进程**无任务**时，顶层还包含：`memoryMonitorRunning`, `taskState`, `verbose`, `allocStepEnabled`, `allocBreakpointsAvailable`。
- **状态聚合**：无 target 时，父进程会对每个非 exited 任务请求 `/api/step-paused`、`/api/last-alloc` 并合并进该 task，在 `tasks[]` 每项中增加可选字段 `paused` (bool)、`pauseReason` (object)、`lastAlloc` (object)。详见「状态聚合」小节。
- **错误**：有 target 但转发失败时由 tryForwardToTarget 返回 502（仅此端点可能 502）；无 target 无错误。

---

### 3.2 GET /api/settings

- **target**：可选。有 target 转发到任务；无 target 时用父进程本地数据；多任务且无 target 返回 400。
- **请求**：无 Body；可选 `?target=`。
- **响应**：`{ "verbose", "logFile", "allocStepEnabled", "allocBreakpointsAvailable", "memoryMonitorRunning" }`。
- **错误**：400（多任务且未指定 target 等）。

---

### 3.3 GET /api/list-dir（仅父进程）

- **用途**：供 Web UI 文件选择器浏览目录；仅父进程注册，子进程不提供。
- **请求**：查询参数 `?dir=<路径>`，缺省为 `"."`（当前工作目录）。
- **响应**：`{ "path": "<规范化的当前路径>", "dirs": [ { "name", "path" } ], "files": [ { "name", "path" } ] }`，`files` 仅包含 `.cml` 文件。若目录不存在或不可访问则返回 `{ "error": "<消息>" }`。
- **错误**：无 HTTP 错误码；错误通过 body 中 `"error"` 字段返回。

---

### 3.4 POST /api/file

- **target**：无。
- **请求**：`{ "path": "<文件路径>" }`。
- **响应**：成功时 `{ "ok": true, "taskId": "<端口号>" }`；失败时 `{ "ok": false, "error": "..." }`（例如文件不存在：`Error: file not found: <path>`）。客户端应提示用户错误并要求重新指定文件。
- **错误**：由 loadSource 命令返回。

---

### 3.5 POST /api/run

- **target**：必选（Web UI 必须显式传）；CLI 可省略则解析为前台任务。Run 只需指明任务 id，path 为任务状态的一部分、不参与区分任务。
- **请求**：`{ "target": "<任务id>", "memoryMonitor": boolean, "allocStep": boolean }`（target 必选供 Web UI；memoryMonitor/allocStep 可选）。alloc 断点由用户在 Memory 页或 CLI 设置，新 worker 或转发 run 后父进程会推送当前 alloc 空间配置一次。
- **响应**：由 launch 命令或 workerRunHandler 返回的 JSON。
- **错误**：由命令或 handler 返回。若指定任务存在且为 `loaded` 则转发 /api/run；否则 fallback 或报错。

---

### 3.6 GET /api/snapshot

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：GC 内存快照 JSON；含 `source`: `"worker"` | `"parent"`。
- **错误**：400（多任务且未指定 target）；解析异常时 body 含 error。

---

（GET /api/step-paused、GET /api/last-alloc 仅子进程内部提供，父进程不注册；Web UI 通过 GET /api/state 的 tasks[].paused、pauseReason、lastAlloc 获取。）

---

### 3.7 GET /api/log

- **target**：可选。**无 target**：仅用本进程 LogSink（父进程 Logger，Debugger tab）。**有 target**：父进程从任务日志缓冲返回该任务 log（Task tab）。
- **请求**：`?offset=N`（可选），`?target=`（可选）。
- **响应**：`{ "lines": string[], "nextOffset": number }`。
- **错误**：有 target 但任务不存在或已退出时 400，body 含 `"error": "No such task or task exited."`。

---

### 3.8 GET /api/breakpoints

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：`{ "alloc": { "spaces": string[] }, ... }`。
- **错误**：400（多任务且未指定 target）。

---

### 3.9 GET /api/breakpoint-types

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：`{ "known": string[], "enabled": string[] }`（known 中不含 alloc_before）。
- **错误**：400；解析异常时 body 含 error。

---

### 3.10 POST /api/breakpoint-types

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "enabled": string[], "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.11 GET /api/breakpoint-spaces

- **target**：可选。有 target 转发；无 target 时若父进程有任务则仍可能用本地 allocBreakSpaces_（实现允许无 target 时返回父进程状态）。
- **请求**：可选 `?target=`。
- **响应**：`{ "breakSpaces": string[] }`，空数组表示不在任何分配上暂停。
- **错误**：400（多任务且未指定 target 时，若 requireWorkerForLocal 触发）。

---

### 3.12 POST /api/breakpoint-spaces

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "breakSpaces": string[], "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.13 POST /api/settings

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "verbose"?: boolean, "logFile"?: string, "target"?: "<id>" }`；logFile 为空则关闭日志文件。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.14 POST /api/continue

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.15 POST /api/restart

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.16 POST /api/terminate

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.17 GET /api/region/:name/memory

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：路径参数 `name`；查询参数 `offset`、`limit`（默认 0, 512）。
- **响应**：region 原始内存字节的 JSON 表示。
- **错误**：400（多任务且未指定 target）；缺少 name 或解析异常时 body 含 error。

---

### 3.18 GET /api/region/:name/objects

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：路径参数 `name`；查询参数 `offset`、`limit`（默认 0, 50）。
- **响应**：region 对象列表分页的 JSON。
- **错误**：400（多任务且未指定 target）；缺少 name 或解析异常时 body 含 error。

---

### 3.19 GET /api/gir-dot

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`，`?path=`。
- **响应**：`{ "ok": true, "dot": string }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：400（多任务且未指定 target）；未配置或 getGirDot 失败时 ok false。

---

## 4. 状态聚合

当客户端请求 **GET /api/state** 且**无 target** 时，父进程在返回前会对 `tasks[]` 中每个 **taskState !== "exited"** 的任务，向该任务端口请求 GET /api/step-paused 与 GET /api/last-alloc，并将结果合并进对应 task 对象：

- **paused** (boolean)：该任务是否当前停在断点。
- **pauseReason** (object | 无)：当 paused 为 true 时，包含 `phase`、`size`、`space`、`ptr` 等（与 step-paused 响应一致）。
- **lastAlloc** (object | 无)：最近一次分配断点信息，用于 UI 展示。

若某任务请求失败或超时，该任务不添加上述字段或使用安全默认。这样前端仅需轮询 GET /api/state 即可获知「运行中 / 停在断点」与 last-alloc，无需单独轮询 step-paused、last-alloc。

---

## 5. 相关文档

- [README.md](../README.md)：使用方式、API 概览。
- [PRODUCT_PRINCIPLES.md](../PRODUCT_PRINCIPLES.md)：产品核心定位与设计原则；API 契约以本文档为准，新增/修改接口需符合本文档中的响应格式与 target 约定。
