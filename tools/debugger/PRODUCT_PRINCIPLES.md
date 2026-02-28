# Camel Debugger 产品核心定位与设计原则

本文档是**产品核心定位与不可违反的设计原则**的单一事实来源，供后续开发与 Agent 自动开发时参考。凡与下列原则冲突的实现或方案均应视为错误，需修正或拒绝。

---

## 一、产品定位

- **Camel Debugger (camel-db)** 是交互式 Camel 调试器，支持 **REPL 与 Web UI 双通道**控制。
- 功能包括：查看 GC 内存布局、按 region 查看十六进制/对象视图、**按类型的调试断点**（如「每分配暂停」）。断点类型由 libcamel 通用 API 统一管理，NDEBUG 构建下零开销，无全局/静态初始化以保持延迟链接友好。
- **多进程架构（默认且唯一）**：Run **始终**在子进程中执行；父进程负责 API 与任务调度，子进程负责脚本执行，二者通过 HTTP 通信。

---

## 二、后端架构原则

### 2.1 命令模式：写操作必须经父进程命令层

- **除纯查询（GET 类）外的所有写操作**，必须经由**父进程的命令分发器（CommandDispatcher）执行**，再在需要时由命令内部通知子进程。
- **禁止「透明转发」**：不允许「有 target 时 HTTP 层直接转发到子进程、父进程不执行命令」的实现。即：不存在「通过 HTTP 调用某功能却对父进程透明」的路径；若有，视为架构缺陷，需重构。
- **正确流程**：Web UI 或 CLI 调用父进程 API → 父进程执行对应命令（更新父进程状态、触发回显）→ 若请求体/参数中有 target，命令内部再调用 `forwardPostToPort` 等通知对应子进程。这样 CLI 与 HTTP 行为一致，且回显始终在父进程发生。

### 2.2 主进程 CLI 前台任务机制

- 多数「按任务」执行的命令（如 continue、restart、terminate、configure、setBreakpointFilter、setBreakpointTypes），在 **CLI 未显式指定 target 时**，默认对**前台任务**执行（由 `getForegroundTaskId()` / `setForegroundTaskId()` 维护）。
- CLI 需提供设定前台任务的命令（如 `task [id]` / `fg [id]`），无参时显示当前前台，有参时设为前台并回显。
- **回显必须标注任务 id**：只要命令实际作用到某个任务（即发生了对某 task 的 forward 或等效操作），回显内容中必须标明该任务 id（例如 `Continue (task 8766).`），以便多任务时用户区分。

### 2.3 父进程与子进程角色

- **父进程**：常驻 API、处理 file/settings/state、spawn/终止子进程；所有写操作先在本进程执行命令并回显，再按 target 通知子进程。
- **子进程**：`--run-worker` 模式，在本机指定端口起 HTTP 服务，执行脚本；子进程内执行的命令回显到该进程的 log，在 Web UI 的 **Task** tab 查看。
- 协议为现有 HTTP API，不引入管道或自定义二进制协议。

### 2.4 父进程为任务状态唯一真相源，不向 Web UI 暴露 502

- **父进程是任务状态的唯一真相源**：子进程存活与状态（running / paused / exited）由父进程维护并对外提供（如 GET /api/state 的 tasks[].taskState）。当父进程无法从子进程拿到合法响应（如子进程已退出）时，应更新本地任务状态（如 setTaskState(port, "exited")），**不向 Web UI 返回 502**。
- **按任务的 GET（如 step-paused、last-alloc）**：有 target 但转发失败时，父进程先更新该任务为 exited（若尚未更新），再返回 **200** 与安全默认 body（如 `{"paused":false}`、`{}`），绝不返回 502。无 target 时不使用「单任务回退」转发，直接返回 200+默认或 400，避免无任务时误转发导致 502。

---

## 三、前端与状态原则

### 3.1 业务状态仅来自后端

- **业务状态**（当前文件、任务列表、断点空间、任务级设置等）**仅来自后端 API**，前端不持久化到 localStorage/sessionStorage；刷新或重开页面后由 API 与当前任务决定展示内容。
- 前端不得用本地存储恢复「已打开脚本列表」「当前任务」等业务数据，避免与后端不一致。

### 3.2 UI 布局偏好可使用存储

- **UI 布局偏好**可使用浏览器 sessionStorage 持久化，例如：主 Tab（Run/Memory/GIR）、侧栏折叠、日志面板高度与折叠。键名需在代码中统一（如 `UI_STORAGE`、`LOG_PANEL_STORAGE_KEY`），且仅用于布局/展示偏好，不承载业务语义。

### 3.3 有打开文件即有任务；前端显式传 target

- **有打开文件即有任务**：打开文件后后端已创建 loaded 任务，前端应有 currentTaskId（来自 state 轮询）。
- 前端**必须显式传 target**（currentTaskId）调用所有任务相关 API，不依赖后端「无 target 用前台」。
- 前端**仅在有当前任务（taskId）时**才发送如 POST breakpoint-spaces、POST /api/run 等按任务的请求；Run 仅传 target 与选项（memoryMonitor、allocStep），不传 path。
- **无当前任务 = 不轮询按任务接口**：仅当存在当前任务时才执行按任务的轮询，避免误用已退出任务导致多余请求与 502。

---

## 四、任务、前台与 API 约定

### 4.1 任务唯一由 id 区分

- **任务 id** 即子进程监听端口号（如 `"8766"`），与 path 无关。同一文件可多次打开，每次新建 worker、新任务。
- **Restart**：杀掉子进程后重新 spawn 时**复用原端口与任务 id**，任务 id 不变。

### 4.2 打开文件即建任务

- **有打开文件即有任务**：POST /api/file 成功后父进程 spawn 子进程并注册任务，状态为 `loaded`，前端据此展示内存页并可设置断点。不存在「用户可设置断点但没有对应 taskId」的情况。

### 4.3 前台机制与 Web UI

- **后端支持前台任务**：某任务可设为前台；新打开的任务可自动设为前台。存在前台时，**允许**命令/API 不显式传 target（由服务端解析为前台任务）。
- **Web UI 不依赖前台**：Web UI 调用 API 时**必须显式传 target**，不依赖「省略 target 用前台」的行为。
- **Run 只认任务 id**：Run 只需指明任务 id（target）；path 仅是任务状态的一部分，用于 UI 展示，不用于区分任务。

### 4.4 API 契约

- **API 契约以 [docs/API.md](docs/API.md) 为准**；新增或修改接口需符合该文档中的响应格式与 target 约定。
- 与任务相关的接口支持 **`target`**（任务 id = 端口）。无 target 时由 `resolveTargetToPort("")` 解析为前台任务；多任务且无 target 且无前台时 400。
- **POST 类（写操作）**：一律在父进程先执行命令、更新状态并回显，再根据 args 中的 target 决定是否 `forwardPostToPort` 到子进程；**禁止**「有 target 时仅转发、父进程不执行命令」的实现。

---

## 五、断点与 libcamel 约定

- 断点由 **libcamel 通用 API**（`include/camel/core/debug_breakpoint.h`）统一管理：注册类型、在代码中 `Hit("类型名", &context)`；NDEBUG 下 `Hit()` 为零开销内联空实现。
- 状态使用函数内 static 懒加载，**无全局/静态初始化**，不影响延迟链接。
- 在内存分配路径中已埋点「分配前/分配后」；新增断点类型只需 RegisterType + Hit，Web UI 的「Breakpoint types」会自动列出。

---

## 六、实施与重构时的检查清单

Agent 或开发者在修改 debugger 相关代码时，应自检：

1. **写操作（POST）**：是否都通过 `dispatchAndRespond` 进入命令层？是否存在「有 target 时直接 tryForwardToTarget 并 return」的写法？若有，需改为「先执行命令，命令内再按 target 调用 forwardPostToPort」。
2. **CLI**：未指定 target 时是否使用前台任务？回显中是否在作用到任务时标注了任务 id？
3. **前端**：是否把业务状态存入了浏览器存储？无 taskId 时是否仍发送按任务的 POST？**无当前任务时是否仍轮询 step-paused / last-alloc？** 若需持久化，是否仅限 UI 布局偏好且键名与约定一致？
4. **多进程**：是否保持了「父进程 = 网关 + 命令执行，子进程 = 运行时」的边界？是否避免了在 HTTP 层做「透明转发」？
5. **父进程状态与 502**：按任务的 GET（如 step-paused、last-alloc）在转发失败时是否返回 502？应改为更新任务状态并返回 200+安全默认。无 target 时是否禁用单任务回退、不转发？

---

## 七、相关文档

- [README.md](README.md)：使用方式、API 概览、目录说明。
- [docs/API.md](docs/API.md)：**API 契约**，请求/响应与 target 约定以此为准。
- 重构方案（命令模式 + 前台任务 + 回显标注）：见项目内重构计划或 `REFACTOR_PLAN.md`（若已落地）。

上述原则与 README、重构方案不一致时，以**本产品核心定位文档**为准；README 与实现应随重构推进更新以符合本文档。
