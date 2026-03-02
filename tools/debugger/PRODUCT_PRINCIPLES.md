# Camel Debugger 产品核心定位与设计原则

本文档是**产品核心定位与不可违反的设计原则**的单一事实来源，供后续开发与 Agent 参考。凡与下列原则冲突的实现或方案均应视为错误，需修正或拒绝。

---

## 一、产品定位

- **Camel Debugger (camel-db)** 是交互式 Camel 调试器，支持 **REPL 与 Web UI 双通道**控制。
- 功能包括：查看 GC 内存布局、按 region 查看十六进制/对象视图、**统一断点模型**（按类型配置与命中，如节点型、分配空间型等）。断点由 libcamel 通用 API 管理，NDEBUG 下零开销，无全局/静态初始化以保持延迟链接友好。
- **多进程架构（默认且唯一）**：Run **始终**在子进程中执行；父进程负责 API 与任务调度，子进程负责脚本执行，二者通过 HTTP 通信。

---

## 二、后端架构原则

### 2.1 写操作必须经父进程命令层

- **除纯查询（GET）外的所有写操作**，必须经由**父进程命令分发器（CommandDispatcher）**执行，再由命令内部在需要时通知子进程。
- **禁止「透明转发」**：不允许「有 target 时 HTTP 层直接转发、父进程不执行命令」的路径。正确流程：Web UI / CLI 调用父进程 API → 父进程执行对应命令（更新父进程状态、回显）→ 若存在 target，命令内部再 `forwardPostToPort` 通知子进程。这样 CLI 与 HTTP 行为一致，回显始终在父进程发生。

### 2.2 父进程与子进程角色

- **父进程**：常驻 API；处理 file、settings、state、spawn/终止子进程；所有写操作先在本进程执行命令并回显，再按 target 通知子进程。**父进程保留断点配置**（各类型），Run/Restart 时将当前断点状态推送给 worker。
- **子进程**：`--run-worker` 模式，在本机指定端口起 HTTP 服务、执行脚本；子进程内执行的命令回显到该进程 log，在 Web UI 的 **Task** tab 查看。
- 协议为现有 HTTP API，不引入管道或自定义二进制协议。

### 2.3 CLI 前台任务

- 按任务执行的命令（continue、restart、terminate、configure、setBreakpointFilter、setBreakpointTypes 等），在 **CLI 未显式指定 target 时**，默认对**前台任务**执行（由 `getForegroundTaskId()` / `setForegroundTaskId()` 维护）。
- CLI 需提供设定前台任务的命令（如 `task [id]` / `fg [id]`）；回显中若实际作用到某任务，必须标明该任务 id（例如 `Continue (task 8766).`）。

### 2.4 父进程为状态唯一真相源；不向 Web UI 暴露 502

- **父进程是任务状态的唯一真相源**：子进程存活与状态（running / paused / exited）由父进程维护并对外提供（如 GET /api/state 的 tasks[]）。当父进程无法从子进程拿到合法响应（如子进程已退出）时，应更新本地任务状态（如 setTaskState(port, "exited")），**不向 Web UI 返回 502**。
- **按任务的 GET**：有 target 但转发失败时，父进程先更新该任务为 exited（若尚未更新），再返回 **200** 与安全默认 body，绝不返回 502。无 target 时不使用「单任务回退」转发，直接返回 200+默认或 400。

---

## 三、前端与状态原则

### 3.1 业务状态仅来自后端

- **业务状态**（当前文件、任务列表、断点配置、任务级设置等）**仅来自后端 API**，前端不持久化到 localStorage/sessionStorage；刷新或重开页面后由 API 与当前任务决定展示内容。前端不得用本地存储恢复「已打开脚本」「当前任务」等业务数据。

### 3.2 UI 布局偏好可使用存储

- **UI 布局偏好**可使用 sessionStorage（如主 Tab、侧栏折叠、日志面板高度与折叠）。键名需在代码中统一（如 `UI_STORAGE`、`LOG_PANEL_STORAGE_KEY`），且仅用于布局/展示偏好，不承载业务语义。

### 3.3 有打开文件即有任务；前端显式传 target

- **有打开文件即有任务**：POST /api/file 成功后父进程 spawn 子进程并注册任务，状态为 `loaded`，前端应有 currentTaskId（来自 state 轮询）。
- 前端**必须显式传 target**（currentTaskId）调用所有任务相关 API，不依赖后端「无 target 用前台」。
- 前端**仅在有当前任务（taskId）时**才发送按任务的请求（如 POST breakpoint-spaces、POST /api/run）；Run 仅传 target，不传 path；断点按统一模型由后端控制。
- **无当前任务 = 不轮询按任务接口**：仅当存在当前任务时才执行按任务的轮询，避免对已退出任务发起请求导致 502。

---

## 四、任务与 API 约定

### 4.1 任务唯一由 id 区分

- **任务 id** 即子进程监听端口号（如 `"8766"`），与 path 无关。同一文件可多次打开，每次新建 worker、新任务。**Restart** 时复用原端口与任务 id。

### 4.2 全局状态与单任务状态分离

- **GET /api/state**：仅返回父进程**全局状态**（serverRunning、hasFile、targetFile、**tasks** 列表及每任务的聚合字段 paused、pauseReason、lastAlloc、assertionError 等），**不接受 target**，不转发。
- **GET /api/task-state?target=&lt;id&gt;**：返回**单个任务**（worker）的 state，与全局 state 结构不同（无 tasks 列表）；需某任务原始 state 时使用。
- 其余与「当前运行任务」相关的接口（snapshot、region、settings、log、断点相关、continue、restart、terminate 等）支持 **target**（GET 用 query，POST 用 body）；带 target 时父进程转发到对应子进程。

### 4.3 API 契约

- **API 契约以 [docs/API.md](docs/API.md) 为准**；新增或修改接口需符合该文档中的请求/响应与 target 约定。
- **POST 类（写操作）**：一律在父进程先执行命令、更新状态并回显，再根据 target 决定是否 `forwardPostToPort`；**禁止**「有 target 时仅转发、父进程不执行命令」的实现。

---

## 五、断点与 libcamel 约定

- **统一断点模型**：断点通过一套标准接口与数据结构表示和管理，按类型配置（如节点型、分配空间型等）；不刻意区分「某种断点」与「另一种断点」，均视为同一模型下的不同类型。父进程收到断点设置请求时保留配置，Run/Restart 时推送给 worker。
- 断点由 **libcamel 通用 API**（`include/camel/core/debug_breakpoint.h`）管理：注册类型、在代码中 `Hit("类型名", &context)`；NDEBUG 下 `Hit()` 为零开销内联空实现；状态使用函数内 static 懒加载，无全局/静态初始化。
- 新增断点类型只需 RegisterType + Hit，Web UI 的「Breakpoint types」等可据此列出；在内存分配路径已埋点「分配前/分配后」等。

---

## 六、实施与重构时的检查清单

修改 debugger 相关代码时，应自检：

1. **写操作（POST）**：是否都通过 `dispatchAndRespond` 进入命令层？是否存在「有 target 时直接 tryForwardToTarget 并 return」的写法？若有，需改为「先执行命令，命令内再按 target forwardPostToPort」。
2. **CLI**：未指定 target 时是否使用前台任务？回显中作用到任务时是否标注了任务 id？
3. **前端**：是否把业务状态存入了浏览器存储？无 taskId 时是否仍发送按任务的 POST？无当前任务时是否仍轮询按任务接口？若需持久化，是否仅限 UI 布局偏好且键名与约定一致？
4. **多进程**：是否保持「父进程 = 网关 + 命令执行，子进程 = 运行时」的边界？是否避免了 HTTP 层「透明转发」？
5. **父进程状态与 502**：按任务的 GET 在转发失败时是否返回 502？应改为更新任务状态并返回 200+安全默认。无 target 时是否禁用单任务回退、不转发？
6. **断点**：父进程是否在收到断点设置请求时保留配置（各类型）？Run/Restart 时是否将当前断点状态（breakpoint-spaces、gir-breakpoints 等）推送给 worker？
7. **状态接口**：GET /api/state 是否始终仅返回全局状态、不接受 target 不转发？单任务详情是否通过 GET /api/task-state?target= 获取？

---

## 七、相关文档

- [README.md](README.md)：使用方式、API 概览、目录说明。
- [docs/API.md](docs/API.md)：**API 契约**，请求/响应与 target 约定以此为准。
- 重构方案（命令模式 + 前台任务 + 回显标注）：见项目内 `REFACTOR_PLAN.md`（若存在）。

上述原则与 README、API 文档或实现不一致时，以**本产品核心定位文档**为准；README 与实现应更新以符合本文档。
