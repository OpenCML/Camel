# Camel Debugger HTTP API 规格

本文档为 camel-db 的 HTTP API 契约，供 Web UI、CLI 与后续开发/Agent 一致遵守。完整请求/响应与 target 约定以此为准。

---

## 1. 概述

- **Base URL**：默认 `http://127.0.0.1:8765`（可通过启动参数修改）。
- **Content-Type**：请求与响应均为 `application/json`。
- **target 通用约定**：
  - **任务 id**：即任务对应子进程监听的**端口号**（如 `"8766"`），与 path 无关；任务唯一由 id 区分。
  - **全局接口**：GET /api/state 仅返回父进程全局状态（任务列表 + 聚合字段），**不接受 target**，不转发。
  - **单任务状态**：GET /api/task-state 必须带 `?target=<任务id>`，转发到该任务并返回其 state（与全局 state 结构不同，无 tasks 列表）。
  - **按任务 GET**：其余按任务接口查询参数 `?target=<任务id>` 可选。
  - **按任务 POST**：Body 中 `"target": "<任务id>"` 可选。
  - **无 target 时**：后端支持**前台任务**；存在前台时由服务端解析为前台任务。多任务且无 target 且无前台时返回 400。**Web UI 调用按任务 API 时必须显式传 target**，不依赖前台机制。
  - 任务是否暂停、last-alloc、assertionError 通过 GET /api/state 的 tasks[] 聚合字段获得，父进程不暴露 GET /api/step-paused、GET /api/last-alloc。
- **打开文件即建任务**：POST /api/file 成功后父进程会 spawn 子进程并注册任务，任务状态为 `loaded`；同一文件可多次打开，每次新建 worker 与新任务。

---

## 2. 响应与错误

- **成功**：多数 GET 直接返回业务 JSON 对象；由 `dispatchAndRespond` 处理的 POST 返回 `{ "ok": true }` 或带业务体的响应；失败时返回 `{ "ok": false, "error": "<消息>" }`。
- **错误体**：统一为 `{ "ok": false, "error": "<消息>" }`（部分端点使用 `"error"` 键）。HTTP 状态码与错误码约定：
  - **400**：参数错误、缺少必选 target（多任务时）、任务不存在或已退出等。
  - **502**：仅当父进程作为网关请求子进程失败且**未**被转换为 200+默认时使用。父进程不暴露 step-paused、last-alloc，客户端仅通过 GET /api/state 获取任务暂停与 last-alloc 信息。

---

## 3. 端点列表

### 3.1 GET /api/state（仅全局状态）

- **用途**：获取父进程全局状态，含任务列表及每个任务的聚合字段。**不接受 target**，不转发。
- **请求**：无 Body；无查询参数（若有 target 也会被忽略）。
- **响应**（父进程）：
  - `serverRunning`: boolean
  - `hasFile`: boolean
  - `targetFile`: string
  - `tasks`: array，每项 `{ "id", "port", "scriptPath", "taskState", ... }`；taskState 为 `"loaded"` | `"running"` | `"paused"` | `"completed"` | `"exited"` 等。
  - 当父进程**无任务**时，顶层还包含：`memoryMonitorRunning`, `taskState`, `verbose`, `allocStepEnabled`, `allocBreakpointsAvailable`。
- **状态聚合**：父进程会对每个非 exited 任务请求该任务的 `/api/step-paused`、`/api/last-alloc`、`/api/state`，将 `paused`、`pauseReason`、`lastAlloc`、`assertionError`（及 assertionExpression/File/Line）合并进对应 task。详见「状态聚合」小节。
- **错误**：无。

---

### 3.2 GET /api/task-state（单任务状态）

- **用途**：获取**单个任务**（worker）的完整 state，与全局 state 结构不同（无 tasks 列表，含 assertionError 等）。仅父进程提供；子进程请直接请求 GET /api/state。
- **请求**：必须带查询参数 `?target=<任务id>`。
- **响应**：转发到该任务 GET /api/state 的原始响应（worker 的 state：serverRunning、hasFile、targetFile、tasks 为空或省略，顶层可能含 assertionError、assertionExpression、assertionFile、assertionLine 等）。
- **错误**：400（缺少 target、任务不存在或已退出）；502（连接 worker 失败或超时）。

---

### 3.3 GET /api/settings

- **target**：可选。有 target 转发到任务；无 target 时用父进程本地数据；多任务且无 target 返回 400。
- **请求**：无 Body；可选 `?target=`。
- **响应**：`{ "verbose", "logFile", "allocStepEnabled", "allocBreakpointsAvailable", "memoryMonitorRunning" }`。
- **错误**：400（多任务且未指定 target 等）。

---

### 3.4 GET /api/list-dir（仅父进程）

- **用途**：供 Web UI 文件选择器浏览目录；仅父进程注册，子进程不提供。
- **请求**：查询参数 `?dir=<路径>`，缺省为 `"."`（当前工作目录）。
- **响应**：`{ "path": "<规范化的当前路径>", "dirs": [ { "name", "path" } ], "files": [ { "name", "path" } ] }`，`files` 仅包含 `.cml` 文件。若目录不存在或不可访问则返回 `{ "error": "<消息>" }`。
- **错误**：无 HTTP 错误码；错误通过 body 中 `"error"` 字段返回。

---

### 3.5 POST /api/file

- **target**：无。
- **请求**：`{ "path": "<文件路径>" }`。
- **响应**：成功时 `{ "ok": true, "taskId": "<端口号>" }`；失败时 `{ "ok": false, "error": "..." }`（例如文件不存在：`Error: file not found: <path>`）。客户端应提示用户错误并要求重新指定文件。
- **错误**：由 loadSource 命令返回。

---

### 3.6 POST /api/run

- **target**：必选（Web UI 必须显式传）；CLI 可省略则解析为前台任务。Run 只需指明任务 id，path 为任务状态的一部分、不参与区分任务。
- **请求**：`{ "target": "<任务id>" }`（target 可选，供 Web UI 指定任务）。run 不接收 memoryMonitor/allocStep；内存扫描与断点按统一断点模型处理，新 worker 或转发 run 后父进程会推送当前断点状态（breakpoint-spaces、gir-breakpoints 等）。
- **响应**：由 launch 命令或 workerRunHandler 返回的 JSON。
- **错误**：由命令或 handler 返回。若指定任务存在且为 `loaded` 则转发 /api/run；否则 fallback 或报错。

---

### 3.7 GET /api/snapshot

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：GC 内存快照 JSON；含 `source`: `"worker"` | `"parent"`。
- **错误**：400（多任务且未指定 target）；解析异常时 body 含 error。

---

（GET /api/step-paused、GET /api/last-alloc 仅子进程内部提供，父进程不注册；Web UI 通过 GET /api/state 的 tasks[].paused、pauseReason、lastAlloc 获取。）

---

### 3.8 GET /api/log

- **target**：可选。**无 target**：仅用本进程 LogSink（父进程 Logger，Debugger tab）。**有 target**：父进程从任务日志缓冲返回该任务 log（Task tab）。
- **请求**：`?offset=N`（可选），`?target=`（可选）。
- **响应**：`{ "lines": string[], "nextOffset": number }`。
- **错误**：有 target 但任务不存在或已退出时 400，body 含 `"error": "No such task or task exited."`。

---

### 3.9 GET /api/breakpoints

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：`{ "alloc": { "spaces": string[] }, ... }`。
- **错误**：400（多任务且未指定 target）。

---

### 3.10 GET /api/breakpoint-types

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：`{ "known": string[], "enabled": string[] }`（known 中不含 alloc_before）。
- **错误**：400；解析异常时 body 含 error。

---

### 3.11 POST /api/breakpoint-types

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "enabled": string[], "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.12 GET /api/breakpoint-spaces

- **target**：可选。有 target 转发；无 target 时若父进程有任务则仍可能用本地状态（父进程保留断点配置，统一模型下 Run/Restart 时推送）。
- **请求**：可选 `?target=`。
- **响应**：`{ "breakSpaces": string[] }`，当前断点配置（分配空间型）；空数组表示该类型未设过滤。
- **错误**：400（多任务且未指定 target 时，若 requireWorkerForLocal 触发）。

---

### 3.13 POST /api/breakpoint-spaces

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "breakSpaces": string[], "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.14 POST /api/settings

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "verbose"?: boolean, "logFile"?: string, "target"?: "<id>" }`；logFile 为空则关闭日志文件。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.15 POST /api/continue

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.16 POST /api/restart

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.17 POST /api/terminate

- **target**：可选（Body）。父进程先执行命令并回显，再按 target 转发到子进程。
- **请求**：`{ "target"?: "<id>" }`。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **错误**：由命令返回。

---

### 3.18 GET /api/region/:name/memory

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：路径参数 `name`；查询参数 `offset`、`limit`（默认 0, 512）。
- **响应**：region 原始内存字节的 JSON 表示。
- **错误**：400（多任务且未指定 target）；缺少 name 或解析异常时 body 含 error。

---

### 3.19 GET /api/region/:name/objects

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：路径参数 `name`；查询参数 `offset`、`limit`（默认 0, 50）。
- **响应**：region 对象列表分页的 JSON。
- **错误**：400（多任务且未指定 target）；缺少 name 或解析异常时 body 含 error。

---

### 3.20 GET /api/gir-json

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`，`?path=`，`?graphId=`。无 `graphId` 时返回根图摘要（id、name、children、dependencies）；有 `graphId` 时返回该图的 nodes、edges 及 children/dependencies 摘要。
- **响应**：`{ "ok": true, "graph": { ... } }` 或 `{ "ok": false, "error": "..." }`。graph 的节点/边 id 为指针地址字符串（如 `"0x1a2b3c4d"`）。
- **错误**：400（多任务且未指定 target）；未配置或 getGirJson 失败时 ok false；graphId 无效时 error "graph not found"。

---

### 3.21 GET /api/gir-breakpoints

- **target**：可选。有 target 转发；无 target 时多任务 400，无任务或单任务用本地数据。
- **请求**：可选 `?target=`。
- **响应**：`{ "nodeIds": ["0x...", ...] }`，当前断点配置（节点型）id 列表。

---

### 3.22 POST /api/gir-breakpoints

- **target**：可选。Body 中 `"target": "<任务id>"` 可选。父进程收到请求时保留一份，Run/Restart 时按统一断点模型推送给 worker。
- **请求**：Body `{ "nodeIds": ["0x...", ...] }`，全量替换当前断点（节点型）列表。
- **响应**：`{ "ok": true }` 或 `{ "ok": false, "error": "..." }`。
- **说明**：执行到所列节点时（需使用 NodeVM 调度器）会暂停，GET /api/state 聚合的 pauseReason 含 `phase: "gir_node"`、`nodeId`、`graphId`。

---

## 4. 状态聚合

当客户端请求 **GET /api/state** 时，父进程在返回前会对 `tasks[]` 中每个 **taskState !== "exited"** 的任务，向该任务端口请求 GET /api/step-paused、GET /api/last-alloc 与 GET /api/state，并将结果合并进对应 task 对象：

- **paused** (boolean)：该任务是否当前停在断点。
- **pauseReason** (object | 无)：当 paused 为 true 时，包含 `phase`、`size`、`space`、`ptr` 等（与 step-paused 响应一致）；若为 GIR 节点断点则 `phase` 为 `"gir_node"`，并含 `nodeId`、`graphId`。
- **lastAlloc** (object | 无)：最近一次分配断点信息，用于 UI 展示。
- **assertionError**、**assertionExpression**、**assertionFile**、**assertionLine**（来自该任务 GET /api/state 的顶层字段）：若该任务发生断言失败则合并进 task，供 UI 展示。

若某任务请求失败或超时，该任务不添加上述字段或使用安全默认。这样前端仅需轮询 GET /api/state 即可获知「运行中 / 停在断点」、last-alloc 与 assertionError，无需单独请求 GET /api/task-state（除非需要该任务的完整 state 原始响应）。

---

## 5. 相关文档

- [README.md](../README.md)：使用方式、API 概览。
- [PRODUCT_PRINCIPLES.md](../PRODUCT_PRINCIPLES.md)：产品核心定位与设计原则；API 契约以本文档为准，新增/修改接口需符合本文档中的响应格式与 target 约定。
