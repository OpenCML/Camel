**问题求解与智能问数** **API** **约定（整合版）**

**下一步计划：** **雪浪云进行评估，再组织业务讨论。**

**整体流程顺序**

| 阶段         | 模块                    | 说明                                                                                                             |
| ------------ | ----------------------- | ---------------------------------------------------------------------------------------------------------------- |
| **问题求解** | 1. 问题定义             | 将自然语言问题结构化，并产出知识链接与评估结果                                                                   |
|              | 2. 方案规划             | 输入 `data.problem_model`，输出 `data.plan`（**`description`** + **`steps`**，`steps[]` 与组件匹配 `step` 同形） |
|              | 3. 问题分解             | 以 **`problem_model`** 为主：方案规划**匹配方案失败**时，将问题模型**细化为更细的树**                            |
|              | 4. 组件匹配             | 仅提交单步 `step`（英文字段）匹配可执行工具及参数，可多步多次调用                                                |
| **智能问数** | 1. 意图识别             | 输出业务域、场景、对象、意图类型与用户目标等                                                                     |
|              | 2. 上下文获取与指标匹配 | 输入结构化意图，输出指标/业务叙述类答案说明                                                                      |
|              | 3. SQL 转换             | Agent/Dify 工具：text2sql，由自然语言生成可执行 SQL（字符串）                                                    |
|              | 4. SQL 执行             | Agent/Dify 工具：sql_execute，在指定库上执行 SQL 并返回 result 行集                                              |
|              | 5. 图表可视化           | 根据查询结果与展示类型生成图表结构与解读文案                                                                     |

 

**组件定位**：

- **问题求解**（`/api/component/problem-solving/`）下 **四个接口**属于**业务流程组件**：按阶段串联「问题结构化 → 方案与规划 → 分解 → 单步工具匹配」等固定编排。
- **智能问数**是**问题求解**的一类**具体业务场景**（典型编排：在「问数」场景下串联意图、上下文与指标、SQL、结果与图表等Agent通用组件）。

 

**一、问题求解**

**路径前缀**：`/api/component/problem-solving/`（下列四接口均在此命名空间下）

| 组件     | 方法 | 路径                                                |
| -------- | ---- | --------------------------------------------------- |
| 问题定义 | POST | `/api/component/problem-solving/problem-definition` |
| 方案规划 | POST | `/api/component/problem-solving/plan`               |
| 问题分解 | POST | `/api/component/problem-solving/decompose`          |
| 组件匹配 | POST | `/api/component/problem-solving/tool-match`         |

**问题求解四组件——响应统一外壳**

下列 **4** 个接口的响应均采用同一顶层结构（建议字段顺序固定为 **`code`** → **`info`** → **`data`**）：

| 字段 | 类型   | 说明                                                                                                |
| ---- | ------ | --------------------------------------------------------------------------------------------------- |
| code | number | **业务状态码**：**`0`** 表示成功；**非 0** 表示异常/失败，具体含义见 **`info`**                     |
| info | string | **`code === 0`** 时恒为 **`""`**；**`code !== 0`** 时给出可读原因或补充说明（错误语义以本字段为准） |
| data | object | 业务载荷；**各组件差异仅体现在 `data` 内**                                                          |

**HTTP 状态码与业务语义**：响应行上的 **HTTP 状态码**（如 **`200`**）仅表示**传输层**是否已正常返回响应体（连接、路由、网关等是否完成交付）。**业务层不依据 HTTP 状态码区分成功/失败或错误类型**；业务结果一律以 body 内的 **`code`**、**`info`**、**`data`** 为准。客户端应据 **`code`/`info`** 做分支与重试，而非用 HTTP 码代替业务码。

**约定**：各节「返回参数」只列 **`data` 内业务字段**；**`details`** 见下节统一说明。

**`data.details`（四接口统一）**：**`data` 内均包含** **`details`**（`object`）。用于扩展信息、调试、实现侧私有字段等，**无固定 schema、不保证向后兼容**。文档**不给出**具体键示例，响应示例中**以空对象 `{}` 占位**（语义上即 **`{...}`**，表示任意 JSON 对象）。

**`info` 约定**：**`code === 0`** 时 `info` 固定为 `""`；**`code !== 0`** 时 `info` 承载异常说明（与 `code` 取值配合使用，具体错误码表由实现/网关约定）。

失败时顶层示例（`data` 形状以各接口为准）：

  ```json
  {
    "code": 1,
    "info": "失败或异常原因（与 code 非 0 对应，见约定）",
    "data": {
      "details": {}
    }
  }
  ```

内层 **`data` 不得再嵌套** 名为 `data` 的外壳；业务对象与 **`details`** 一律作为 `data` 的平级子字段（如 `problem_model`、`plan`、`tools` 等，因接口而异）。**`details`** 含义见上表「`data.details`（四接口统一）」。

**`problem_model` 约定（四接口）**：本段各接口**请求/响应**中出现的 **`problem_model` 内部字段尚未最终确定**，可能随实现迭代增删或调整。**建议前端不要依赖具体字段名或嵌套形态**；跨步骤编排时，将上游返回的 **`problem_model` 作为整体原样透传**至下游请求（不拆解、不按固定 schema 强绑定展示或存储）。

**1.** **问题定义**

•     **接口**：`POST /api/component/problem-solving/problem-definition`

  HTTP   POST  /api/component/problem-solving/problem-definition

  ```json
  {
    "question": "公开采购与非公开采购金额构成如何？"
  }
  ```

**请求参数**：

| 参数     | 类型   | 必填 | 说明                   |
| -------- | ------ | ---- | ---------------------- |
| question | string | 是   | 用户输入的自然语言问题 |

**返回参数**：

| 字段          | 类型    | 说明                                                                                |
| ------------- | ------- | ----------------------------------------------------------------------------------- |
| pass          | boolean | 问题是否通过评估                                                                    |
| problem_model | object  | **树形**问题模型（根节点），供方案规划等后续环节输入；**字段 key 均为英文**，见下表 |
| details       | object  | 见上文「**`data.details`（四接口统一）**」；示例用 **`{}`** 占位                    |

**`problem_model` 字段**（下表为**示意**，非锁定契约；字段稳定性见上文「**`problem_model` 约定（四接口）**」）：

| 字段             | 类型   | 说明                                                                                        |
| ---------------- | ------ | ------------------------------------------------------------------------------------------- |
| problem_id       | string | 问题唯一标识                                                                                |
| description      | string | 问题描述                                                                                    |
| problem_type     | string | 问题类型                                                                                    |
| constraints      | array  | 约束列表                                                                                    |
| target_object    | string | 目标对象                                                                                    |
| stakeholders     | array  | 干系人                                                                                      |
| goals            | array  | 目标列表                                                                                    |
| status           | string | 当前状态                                                                                    |
| created_at       | string | 创建时间                                                                                    |
| related_entities | array  | 相关实体                                                                                    |
| children         | array  | **子节点**列表；与根节点**同形**（递归树）。**当前实现仅返回单一根节点**，故一般为 **`[]`** |

**说明**：`problem_model` 语义上为**树**；子问题以 `children` 嵌套展开。现网实现尚未填充子树，响应中通常只见根节点且 **`children` 为空**。

**响应示例**：

说明：`data.problem_model` 供方案规划等组件输入。**`details`** 见统一约定，示例用 **`{}`** 占位。

  ```json
  {
    "code": 0,
    "info": "",
    "data": {
      "pass": true,
      "problem_model": {
        "problem_id": "QKB-001",
        "description": "公开采购与非公开采购金额构成如何？",
        "problem_type": "描述型问题",
        "constraints": ["需基于公开数据源"],
        "target_object": "采购金额构成",
        "stakeholders": ["采购部门", "财务部门"],
        "goals": ["明确金额分配比例"],
        "status": "分析阶段",
        "created_at": "2026-04-08T00:54:30.393365",
        "related_entities": ["公开采购", "非公开采购", "金额构成"],
        "children": []
      },
      "details": {}
    }
  }
  ```

 

**2.** **方案规划**

•     **接口**：`POST /api/component/problem-solving/plan`

编排说明：本接口**仅接受问题模型**，请求体**只有** **`problem_model`**（**不包含** **`steps`** 或其它步骤列表）。**`steps`** **只出现在响应** **`data.plan.steps`** 中，由服务生成。入参即 **问题定义**（或经 **问题分解** 细化后）响应里的 **`data.problem_model`**：字段名仍为 `problem_model`，**其值须与上游一致**（原样透传，不另构造「query」或其它形态）。服务内部可再跑意图识别等逻辑，但**对编排而言**，输入就是上游产出的那一棵 `problem_model`。

**响应中的 `data.problem_model`**：**与输入同形**（树形，字段见 **问题定义**）。**若能直接匹配到可用方案**，对请求里的 `problem_model` **原样透传**返回。**若无法直接匹配**，方案规划组件可**自由探索**并**重构**问题模型（改写描述、补全约束、调整子树等），此时返回的 `problem_model` **可能与请求不一致**；后续环节（问题分解、组件匹配等）应以**本次响应中的** `data.problem_model` 与 **`data.plan`**（含 **`description`**、**`steps`**）为准。

**请求参数**（**仅此一项**，无 `steps` / `plan` 等）：

| 参数名        | 类型 | 必填 | 说明                                                                                               |
| ------------- | ---- | ---- | -------------------------------------------------------------------------------------------------- |
| problem_model | json | 是   | **等于**上游 **`data.problem_model`**（通常来自 **问题定义**；树形，含 `children` 等，字段见该节） |

**请求示例**（与 **问题定义** 响应示例中 `data.problem_model` 对齐）：

  ```json
  {
    "problem_model": {
      "problem_id": "QKB-001",
      "description": "公开采购与非公开采购金额构成如何？",
      "problem_type": "描述型问题",
      "constraints": ["需基于公开数据源"],
      "target_object": "采购金额构成",
      "stakeholders": ["采购部门", "财务部门"],
      "goals": ["明确金额分配比例"],
      "status": "分析阶段",
      "created_at": "2026-04-08T00:54:30.393365",
      "related_entities": ["公开采购", "非公开采购", "金额构成"],
      "children": []
    }
  }
  ```

**工作流程**：

1. 接收 **问题定义** 输出的 **`data.problem_model`**（见上）。
2. 生成 **`data.plan`**（**`description`** + **`steps`**，见下表）；同时在 **`data.problem_model`** 中回传当前采纳的问题模型——**能直接匹配方案时透传输入**，否则返回**重构后**的问题模型（见编排说明）。

**返回参数**：

| 参数名                   | 类型    | 说明                                                                                                                                                                                                              |
| ------------------------ | ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| pass                     | boolean | 是否通过：找不到方案返回 false                                                                                                                                                                                    |
| problem_model            | object  | **树形问题模型**，与 **问题定义** `problem_model` **同形**。**能直接匹配方案**：对请求体 `problem_model` **透传**。**无法直接匹配**：组件**自由探索并重构**，返回值可与请求不一致，下游以本字段为准               |
| plan                     | object  | 方案输出；固定包含 **`description`** 与 **`steps`**                                                                                                                                                               |
| plan.description         | string  | 方案级文字说明，**可注入 LLM 上下文**，补充领域背景、流程意图等非单步信息（与单步 `step` 并列使用）                                                                                                               |
| plan.steps               | array   | 有序步骤列表。**每一项**与 **组件匹配** 请求体中的 **`step` 同形**（`objective` / `inputs` / `outputs` / `constraints`）；编排可对 **每一步**调用组件匹配，将 `{ "step": steps[i] }` 作为请求体以获取该步工具推荐 |
| plan.steps[].objective   | string  | 该步目标                                                                                                                                                                                                          |
| plan.steps[].inputs      | array   | 输入列表                                                                                                                                                                                                          |
| plan.steps[].outputs     | array   | 输出列表                                                                                                                                                                                                          |
| plan.steps[].constraints | array   | 约束列表                                                                                                                                                                                                          |
| details                  | object  | 见上文「**`data.details`（四接口统一）**」；示例用 **`{}`** 占位                                                                                                                                                  |

**编排提示**：对 **`data.plan.steps`** 中**每一个**元素 `s`，向 **组件匹配** 发送请求体 **`{ "step": s }`**（与单步字段完全一致），即可得到该步的工具推荐；**`data.plan.description`** 可与 **`data.problem_model`** 等一并注入 **LLM** 作为会话/计划层背景，**不替代**单步 `step` 字段。

**响应示例**（**可直接匹配方案**：`data.problem_model` 与请求透传一致）：

  ```json
  {
    "code": 0,
    "info": "",
    "data": {
      "pass": true,
      "problem_model": {
        "problem_id": "QKB-001",
        "description": "公开采购与非公开采购金额构成如何？",
        "problem_type": "描述型问题",
        "constraints": ["需基于公开数据源"],
        "target_object": "采购金额构成",
        "stakeholders": ["采购部门", "财务部门"],
        "goals": ["明确金额分配比例"],
        "status": "分析阶段",
        "created_at": "2026-04-08T00:54:30.393365",
        "related_entities": ["公开采购", "非公开采购", "金额构成"],
        "children": []
      },
      "plan": {
        "description": "面向采购金额构成分析：依次完成意图到指标、SQL 生成与执行、再可视化与解读。可将本段与 problem_model 一并作为 LLM 编排上下文。",
        "steps": [
          {
            "objective": "将结构化意图转换为指标描述的业务逻辑",
            "inputs": ["结构化意图"],
            "outputs": ["指标描述的业务逻辑"],
            "constraints": [
              "基于业务指标体系匹配",
              "意图语义准确解析",
              "指标定义无歧义"
            ]
          },
          {
            "objective": "将指标描述的业务逻辑转换为可执行 SQL 语句",
            "inputs": ["指标描述的业务逻辑"],
            "outputs": ["SQL 描述的业务查询语句"],
            "constraints": [
              "SQL 语法规范合法",
              "表字段与指标一一对应",
              "避免逻辑错误与性能风险"
            ]
          },
          {
            "objective": "执行 SQL 语句并完成指标数值计算",
            "inputs": ["SQL 查询语句"],
            "outputs": ["指标计算结果数据集"],
            "constraints": [
              "数据源权限合法",
              "计算结果准确无误",
              "执行超时控制"
            ]
          },
          {
            "objective": "基于指标计算结果生成可视化图表与自然语言答案",
            "inputs": ["指标计算结果数据集"],
            "outputs": ["可视化图表", "自然语言文字描述内容"],
            "constraints": [
              "图表类型与数据匹配",
              "描述语言简洁易懂",
              "结果与计算数据一致"
            ]
          }
        ]
      },
      "details": {}
    }
  }
  ```

**无法直接匹配方案时**（无单独 JSON 示例）：`data.problem_model` 为**重构后**模型，字段与请求可能不一致；**`data.plan.description` / `data.plan.steps`** 仍按上表语义生成并与本次 `data.problem_model` 对齐，下游勿再假定与 **问题定义** 原始输出逐字段相同。可再经 **问题分解** 将 **`problem_model` 树**进一步**细化**后，再尝试 **方案规划** 或下游编排。

 

**3.** **问题分解**

•     **接口**：`POST /api/component/problem-solving/decompose`

**语义**：响应以 **`data.problem_model`** 为主——在 **问题定义** 产出的问题模型基础上，将**树形结构进一步细化**（子问题写入 `children`，可多层级）。典型用法：**方案规划**阶段**无法直接匹配到可用方案**（如 `pass: false` 或无可行 `plan`）时，调用本组件把问题模型**拆细**，再带着**更精细的树**回到 **方案规划** 或直接下游编排。

**请求参数**：

请求体**仅包含** `problem_model`，**不接收** `plan`（与 **方案规划** 解耦）。

| 参数名        | 类型 | 必填 | 说明                                                                                                                                                                                   |
| ------------- | ---- | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| problem_model | json | 是   | 待细化的问题模型，与 **问题定义** `problem_model` **同形**；通常取 **问题定义** `data.problem_model`，或与 **方案规划** 请求/响应中的 `problem_model` **对齐**（重构后以方案规划为准） |

**请求示例**：

  ```json
  {
    "problem_model": {
      "problem_id": "QKB-001",
      "description": "公开采购与非公开采购金额构成如何？",
      "problem_type": "描述型问题",
      "constraints": ["需基于公开数据源"],
      "target_object": "采购金额构成",
      "stakeholders": ["采购部门", "财务部门"],
      "goals": ["明确金额分配比例"],
      "status": "分析阶段",
      "created_at": "2026-04-08T00:54:30.393365",
      "related_entities": ["公开采购", "非公开采购", "金额构成"],
      "children": []
    }
  }
  ```

**返回参数**：

| 参数名              | 类型   | 说明                                                                                                                                                                         |
| ------------------- | ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| problem_model       | object | **主**：细化后的**树形**问题模型，与 **问题定义** 同形；`children` 内为更细的子问题节点（可嵌套）                                                                            |
| steps               | array  | **可选**：从树**展开**的线性步骤，元素与 **组件匹配** 的 **`step` 同形**（`objective` / `inputs` / `outputs` / `constraints`），便于逐步调用组件匹配；无则编排可据树自行遍历 |
| steps[].objective   | string | 该步目标                                                                                                                                                                     |
| steps[].inputs      | array  | 输入列表                                                                                                                                                                     |
| steps[].outputs     | array  | 输出列表                                                                                                                                                                     |
| steps[].constraints | array  | 约束列表                                                                                                                                                                     |
| steps[].order       | number | **可选**，序号，仅辅助展示                                                                                                                                                   |
| steps[].depends_on  | array  | **可选**，依赖（**待定**）                                                                                                                                                   |
| details             | object | 见上文「**`data.details`（四接口统一）**」；示例用 **`{}`** 占位                                                                                                             |

**响应示例**（`problem_model` 含子节点；`steps` 可选）：

  ```json
  {
    "code": 0,
    "info": "",
    "data": {
      "problem_model": {
        "problem_id": "QKB-001",
        "description": "公开采购与非公开采购金额构成如何？",
        "problem_type": "描述型问题",
        "constraints": ["需基于公开数据源"],
        "target_object": "采购金额构成",
        "stakeholders": ["采购部门", "财务部门"],
        "goals": ["明确金额分配比例"],
        "status": "分析阶段",
        "created_at": "2026-04-08T00:54:30.393365",
        "related_entities": ["公开采购", "非公开采购", "金额构成"],
        "children": [
          {
            "problem_id": "QKB-001-A",
            "description": "厘清公开/非公开采购口径与数据来源",
            "problem_type": "澄清型子问题",
            "constraints": [],
            "target_object": "统计口径",
            "stakeholders": [],
            "goals": [],
            "status": "待分析",
            "created_at": "2026-04-08T00:54:30.393365",
            "related_entities": [],
            "children": []
          },
          {
            "problem_id": "QKB-001-B",
            "description": "在口径一致前提下计算金额构成",
            "problem_type": "计算型子问题",
            "constraints": [],
            "target_object": "金额构成",
            "stakeholders": [],
            "goals": [],
            "status": "待分析",
            "created_at": "2026-04-08T00:54:30.393365",
            "related_entities": [],
            "children": []
          }
        ]
      },
      "steps": [],
      "details": {}
    }
  }
  ```

**4.** **组件匹配**

•     **接口**：`POST /api/component/problem-solving/tool-match`

针对**某一个** `step`，返回**一组**该步骤**可能用到的**工具（响应 **`data.tools`**）。**多数情况下**该数组**只有一项**；少数情况会返回多项，**这些项可能都需要用到**，**数组顺序与 `confidence` 无必然关系**（顺序由实现约定，可不保证稳定）。

请求体**仅包含** `step`。多次调用即可覆盖多步（每步一次），避免一次返回整链路的匹配结果。

**每条**返回的工具须满足 **Agent tool call schema**（`name`、`description`、`parameters` 为 JSON Schema），并带有 **`confidence`**。在此之外另有 **`similar_options`**：存放与**当前这条工具**语义相近的**备选**工具，每项结构同样满足上述 schema（一般**不再嵌套** `similar_options` 或嵌套恒为空）。**多数场景下 `similar_options` 为空数组**。

**请求参数**：

| 参数名 | 类型 | 必填 | 说明                                                                                                         |
| ------ | ---- | ---- | ------------------------------------------------------------------------------------------------------------ |
| step   | json | 是   | **单步**方案描述；字段 key 为英文，见下表（与 **方案规划** `data.plan.steps[]` **每一项**同形，不含任何 id） |

**step 字段**：

| 字段        | 类型   | 说明     |
| ----------- | ------ | -------- |
| objective   | string | 方案目标 |
| inputs      | array  | 输入列表 |
| outputs     | array  | 输出列表 |
| constraints | array  | 约束列表 |

**请求示例**：

  ```json
  {
    "step": {
      "objective": "将结构化意图转换为指标描述的业务逻辑",
      "inputs": ["结构化意图"],
      "outputs": ["指标描述的业务逻辑"],
      "constraints": [
        "基于业务指标体系匹配",
        "意图语义准确解析",
        "指标定义无歧义"
      ]
    }
  }
  ```

**返回参数**：

| 参数名                  | 类型   | 说明                                                                                                                       |
| ----------------------- | ------ | -------------------------------------------------------------------------------------------------------------------------- |
| tools                   | array  | 该 step **可能用到的**工具列表；**多数情况仅 1 条**。                                                                      |
| tools[]                 | object | 主推荐工具：Agent tool schema + `confidence` + `similar_options`                                                           |
| tools[].name            | string | 工具名称（机器可读标识）                                                                                                   |
| tools[].description     | string | 工具能力说明，供编排 / LLM 理解                                                                                            |
| tools[].parameters      | object | 入参 JSON Schema，通常为 `type: "object"`，含 `properties`、`required`、`additionalProperties` 等                          |
| tools[].confidence      | number | 可选，**该条**工具与当前 **step** 的匹配置信度（如 0～1）；**不用于** `tools` 数组排序，无则省略                           |
| tools[].similar_options | array  | 与**本条主工具**相近的备选工具；元素与主工具**同形**（`name`、`description`、`parameters`、`confidence`），**一般为 `[]`** |
| details                 | object | 见上文「**`data.details`（四接口统一）**」；示例用 **`{}`** 占位                                                           |

`parameters` 形态与常见 Agent / Function Calling 约定一致，示例见下。

**响应示例**：

  ```json
  {
    "code": 0,
    "info": "",
    "data": {
      "tools": [
        {
          "name": "search_web",
          "description": "Search the web for current information.",
          "parameters": {
            "type": "object",
            "properties": {
              "query": {
                "type": "string",
                "description": "The search query."
              },
              "top_k": {
                "type": "integer",
                "description": "Number of results to return",
                "default": 5
              }
            },
            "required": ["query"],
            "additionalProperties": false
          },
          "confidence": 0.92,
          "similar_options": []
        }
      ],
      "details": {}
    }
  }
  ```

  **`similar_options` 非空**（少见）：备选与主工具同形；备选内一般不再带 `similar_options`。

  ```json
  {
    "code": 0,
    "info": "",
    "data": {
      "tools": [
        {
          "name": "search_web",
          "description": "Search the web for current information.",
          "parameters": {
            "type": "object",
            "properties": {
              "query": {
                "type": "string",
                "description": "The search query."
              }
            },
            "required": ["query"],
            "additionalProperties": false
          },
          "confidence": 0.92,
          "similar_options": [
            {
              "name": "search_web_alt",
              "description": "备用网页检索实现。",
              "parameters": {
                "type": "object",
                "properties": {
                  "query": { "type": "string", "description": "检索词" }
                },
                "required": ["query"],
                "additionalProperties": false
              },
              "confidence": 0.78
            }
          ]
        }
      ],
      "details": {}
    }
  }
  ```

**问题求解链路小结**：

1. 问题定义 → 结构化问题与图谱链接
2. 方案规划 → 承接 **问题定义** 的 **`data.problem_model`**（请求体 `problem_model`），产出 **`data.plan`**（**`description`** + **`steps`**；**`steps[]` 每项**可单独送入组件匹配），并在 **`data.problem_model`** 中透传或回传**重构后**的问题模型
3. 问题分解 → 仅提交 **`problem_model`** → 主产出 **`data.problem_model`**（更细的树）；**方案规划匹配方案失败**时常用此步细化后再规划
4. 组件匹配 → 每次仅提交 `step`（英文字段）→ 返回该步的 `tools` 列表（可多次调用）

 

**二、智能问数**

**智能问数——响应统一外壳（自意图识别起）**：本节接口的响应采用与上文「**问题求解四组件——响应统一外壳**」**相同的顶层结构**：**`code`**（`number`，**`0`** 成功）→ **`info`**（`string`，成功为 **`""`**）→ **`data`**（`object`，业务载荷）。**HTTP 状态码**仅表示传输层是否交付响应体，**业务语义以 body 内 `code` / `info` / `data` 为准**（详见该节）。**`data` 内业务字段**见各小节「返回参数」；**`data.details`** 与问题求解相同（扩展/调试用，**无固定 schema**），示例以 **`{}`** 占位。

**1.** **意图识别**

•     **接口名**：`POST /api/component/intent-recognition/deep-recognize`

**请求参数**：

| 参数名        | 类型 | 必填 | 说明                                                                   |
| ------------- | ---- | ---- | ---------------------------------------------------------------------- |
| problem_model | json | 是   | 树形问题模型（与 **问题定义** 中 `problem_model` 同形，含 `children`） |

**请求示例（****JSON****）**：

  ```json
  {
    "problem_model": {
      "problem_id": "QKB-001",
      "description": "公开采购与非公开采购金额构成如何？",
      "problem_type": "描述型问题",
      "constraints": ["需基于公开数据源"],
      "target_object": "采购金额构成",
      "stakeholders": ["采购部门", "财务部门"],
      "goals": ["明确金额分配比例"],
      "status": "分析阶段",
      "created_at": "2026-04-09T00:54:30.393365",
      "related_entities": ["公开采购", "非公开采购", "金额构成"],
      "children": []
    }
  }
  ```

**返回参数**（下列字段均在 **`data`** 内；顶层为 **`code`** / **`info`** / **`data`**，见上节「智能问数——响应统一外壳」）：

| 参数名        | 类型    | 说明                                                                                                    |
| ------------- | ------- | ------------------------------------------------------------------------------------------------------- |
| understanding | string  | 理解说明                                                                                                |
| pass          | boolean | 是否通过                                                                                                |
| intent        | object  | 结构化意图，含 `intent_id`、`domain`、`scenario`、`object`、`intent_name`、`user_goal` 等（见响应示例） |
| details       | object  | 见上文「**`data.details`**」约定；示例用 **`{}`** 占位                                                  |

**响应示例**（成功：`code === 0`）：

  ```json
  {
    "code": 0,
    "info": "",
    "data": {
      "understanding": "最终意图识别成功",
      "pass": true,
      "intent": {
        "intent_id": "INT20260211001",
        "domain": "采购管理域",
        "scenario": "采购结构分析",
        "object": "采购金额结构",
        "intent_name": "看构成",
        "user_goal": "分析公开与非公开采购金额构成"
      },
      "details": {}
    }
  }
  ```

失败时顶层结构与 **问题求解** 相同（**`code !== 0`**，`info` 说明原因，`data` 形状以实现为准，可含 **`details`**）。

 

**2.** **上下文获取与指标匹配**

•     **接口**：POST /api/component/answer-generation/tools/answer-generation

编排说明：本节入参为结构化 **intent_output**（字段名为 PascalCase）。编排层需将**意图识别**响应 **`data.intent`**（及 **`understanding`** 等）映射为下列结构（例如 domain → Domain，intent_name → IntentType，user_goal → Goal 等），以保证与 Mock 一致。

  HTTP   POST  /api/component/answer-generation/tools/answer-generation

  ```json
  {
    "intent_output": {
      "Domain": "采购管理域",
      "Scenario": "采购结构分析",
      "Object": "采购金额结构",
      "Goal": "公开采购与非公开采购金额构成如何？",
      "IntentType": "看构成"
    }
  }
  ```

**请求参数**：

| 参数          | 类型   | 必填 | 说明                                   |
| ------------- | ------ | ---- | -------------------------------------- |
| intent_output | object | 是   | 意图识别的结构化输出，须包含 Goal 字段 |

**intent_output** **字段**：

| 字段       | 类型   | 说明            |
| ---------- | ------ | --------------- |
| Domain     | string | 业务域          |
| Scenario   | string | 业务场景        |
| Object     | string | 业务对象        |
| Goal       | string | 用户的问题/目标 |
| IntentType | string | 意图类型        |

**响应示例**：

  ```json
  {
    "success": true,
    "answer": "根据业务模型识别到该问题对应的业务活动是：公开采购与非公开采购金额对比分析\n根据指标模型匹配：CG_003 公开采购中标金额\n推导：公开采购金额可通过指标CG_003计算得出（SQL: SELECT SUM(gkcaigouamount) AS value FROM ads_dzcg_bidsum  WHERE year = #{year}），非公开采购金额需补充相关指标数据\n因此，业务被转化为用指标描述的业务，最终答案：公开采购金额通过CG_003指标计算，非公开采购金额需补充对应指标数据才能完成构成分析"
  }
  ```

 

**3. SQL** **转换（****Dify** **工具：****database****）**

本节为 **SQL** **转换**独立模块。Agent **工具调用**形态：外层以工具名为键、值为具体入参；响应同样以工具名为键。与上一节 HTTP 组件接口并存，由编排层选择调用 Dify/Agent 工具或后端 HTTP。

•     **工具名**：text2sql

•     **说明**：根据数据库连接与自然语言问题，生成可执行 SQL（返回为字符串，常含推理说明与最终 SQL 片段）。

**请求参数（****text2sql** **对象内字段）**：

| 参数名 | 类型   | 必填 | 说明                                                      |
| ------ | ------ | ---- | --------------------------------------------------------- |
| db_uri | string | 是   | 数据库连接 URI，如 mysql+pymysql://user:pass@host:port/db |
| query  | string | 是   | 用户自然语言问句（中文或英文均可）                        |

**请求示例**：

  ```json
  {
    "text2sql": {
      "db_uri": "mysql+pymysql://user:123456@host:port/rail_assistant",
      "query": "公开采购与非公开采购金额构成如何"
    }
  }
  ```

**返回参数**：

| 参数名   | 类型   | 说明                                                                         |
| -------- | ------ | ---------------------------------------------------------------------------- |
| text2sql | string | 模型输出整段文本：可含推理说明及最终 **SQL**。调用方需解析或截取可执行 SQL。 |

**返回示例（字符串内容形态）**：

  Plaintext   （可选：模型对表名、字段含义、聚合与占比逻辑的推理说明……）      SELECT     SUM(CAST(gkcaigouamount AS DECIMAL))  AS public_procurement_amount,     SUM(CAST(caigouAmount AS DECIMAL)) -  SUM(CAST(gkcaigouamount AS DECIMAL)) AS non_public_procurement_amount,     SUM(CAST(caigouAmount AS DECIMAL)) AS  total_procurement_amount,     (SUM(CAST(gkcaigouamount AS DECIMAL))  / SUM(CAST(caigouAmount AS DECIMAL))) * 100 AS public_procurement_percentage,      ((SUM(CAST(caigouAmount AS DECIMAL))  - SUM(CAST(gkcaigouamount AS DECIMAL))) / SUM(CAST(caigouAmount AS DECIMAL)))  * 100 AS non_public_procurement_percentage    FROM ads_dzcg_bidsum    WHERE caigouAmount IS NOT NULL AND gkcaigouamount IS NOT NULL    LIMIT 5;  

**JSON** **包裹后的返回示例**：

  ```json
  {
    "text2sql": "（推理说明……）\n\nSELECT SUM(CAST(gkcaigouamount AS DECIMAL)) AS public_procurement_amount, ... LIMIT 5;"
  }
  ```

**本模块工具摘要**：

| 工具名   | 请求主结构     | 响应主结构             |
| -------- | -------------- | ---------------------- |
| text2sql | db_uri + query | 单字符串（推理 + SQL） |

 

**4. SQL** **执行（****Dify** **工具：****database****）**

本节为 **SQL** **执行**独立模块，与 **SQL** **转换** 衔接：通常以上一节 text2sql 产出（经解析）的 SQL 作为本模块 query 入参。调用形态与上一节相同（工具名为键的对象）。

•     **工具名**：sql_execute

•     **说明**：在指定数据库上执行 SQL，返回 JSON 字符串形式的查询结果（内含 result 行集）。

**请求参数（****sql_execute** **对象内字段）**：

| 参数名 | 类型   | 必填 | 说明                             |
| ------ | ------ | ---- | -------------------------------- |
| db_uri | string | 是   | 数据库连接 URI，与 text2sql 一致 |
| query  | string | 是   | 待执行的 SQL 语句                |

**请求示例**：

  ```json
  {
    "sql_execute": {
      "db_uri": "mysql+pymysql://user:123456@host:port/rail_assistant",
      "query": "SELECT \n SUM(CAST(gkcaigouamount AS  DECIMAL)) AS public_procurement_amount,\n   SUM(CAST(caigouAmount AS DECIMAL)) - SUM(CAST(gkcaigouamount AS  DECIMAL)) AS non_public_procurement_amount,\n   SUM(CAST(caigouAmount AS DECIMAL)) AS total_procurement_amount\nFROM  ads_dzcg_bidsum;"
    }
  }
  ```

**返回参数**：

| 参数名      | 类型   | 说明                                |
| ----------- | ------ | ----------------------------------- |
| sql_execute | string | **JSON** **字符串**。解析后见下表。 |

**内层结构（对** **sql_execute** **做** **JSON.parse** **后）**：

| 参数名 | 类型  | 说明                                       |
| ------ | ----- | ------------------------------------------ |
| result | array | 查询结果行列表，每行为对象，键为列名或别名 |

**注意**：数值列可能被序列化为字符串（如 "693475000"），Mock 或前端需按业务转换类型。

**外层响应示例**：

  ```json
  {
    "sql_execute": "{\"result\": [{\"non_public_procurement_amount\": \"216830140\", \"public_procurement_amount\": \"476644860\", \"total_procurement_amount\": \"693475000\"}]}"
  }
  ```

**解析后**：

  ```json
  {
    "result": [
      {
        "non_public_procurement_amount": "216830140",
        "public_procurement_amount": "476644860",
        "total_procurement_amount": "693475000"
      }
    ]
  }
  ```

**本模块工具摘要**：

| 工具名      | 请求主结构     | 响应主结构                 |
| ----------- | -------------- | -------------------------- |
| sql_execute | db_uri + query | JSON 字符串：result[] 行集 |

 

**5.** **图表可视化**

•     **模块**：图表生成

•     **接口名**：chart-generator

根据查询数据、指标、维度生成可渲染图表结构，支持柱状图、折线图、饼图、文本说明等。

**请求参数**：

| 参数名       | 类型   | 必填 | 说明                                                                                               |
| ------------ | ------ | ---- | -------------------------------------------------------------------------------------------------- |
| 待处理的数据 | string | 是   | 查询结果序列化后的字符串（示例为 JSON 数组字符串）；与 queryResult（object）语义一致时实现可二选一 |
| 图表标题     | string | 否   | 图表标题                                                                                           |
| 图表类型     | string | 否   | 如 pie / bar / line，对应 displayType：bar / line / pie / text                                     |
| 配色方案     | string | 否   | 如 default                                                                                         |

**请求示例**：

  `待处理的数据` 为 JSON 数组的字符串形式，下例中内层数组已展开便于阅读，实际传参仍为单行转义字符串。

  ```json
  {
    "待处理的数据": "[{\"采购类型\":\"公开采购\",\"金额\":477000000,\"占比\":68.8},{\"采购类型\":\"非公开采购\",\"金额\":217000000,\"占比\":31.2}]",
    "图表标题": "公开采购与非公开采购金额构成",
    "图表类型": "pie",
    "配色方案": "default"
  }
  ```

  内层数据等价于：

  ```json
  [
    { "采购类型": "公开采购", "金额": 477000000, "占比": 68.8 },
    { "采购类型": "非公开采购", "金额": 217000000, "占比": 31.2 }
  ]
  ```

**响应示例**：

  ```json
  {
    "code": 200,
    "message": "生成成功",
    "chartData": {
      "title": {
        "text": "公开采购与非公开采购金额构成"
      },
      "series": [
        {
          "type": "pie",
          "radius": "55%",
          "data": [
            { "name": "公开采购", "value": 477000000 },
            { "name": "非公开采购", "value": 217000000 }
          ]
        }
      ]
    },
    "analysis": "公开采购金额占比68.8%，为主要采购方式，非公开采购占比31.2%。"
  }
  ```

 

**附录：接口与工具速览**

| 流程段       | 路径（均在 `/api/component/problem-solving/` 下）  | 方法 / 请求体摘要                                           | 返回主结构（摘要）                                                                                            |
| ------------ | -------------------------------------------------- | ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| 问题定义     | `problem-definition`                               | POST                                                        | 统一：`code`（0 成功）+ `info` + `data`（`pass`、`problem_model` 树形含 `children`，及 **`details`** 见正文） |
| 方案规划     | `plan`                                             | POST，body：`problem_model` = 问题定义 `data.problem_model` | 统一外壳；`data.pass` + `data.problem_model` + `data.plan`（`description` + `steps`）+ **`details`**          |
| 问题分解     | `decompose`                                        | POST，body：仅 **`problem_model`**                          | 统一外壳；**主：**`data.problem_model`（细化树）；可选 `data.steps`；**`details`**                            |
| 组件匹配     | `tool-match`                                       | POST，body：仅 **`step`**                                   | 统一外壳；`data.tools`[]（每步单独请求）+ **`details`**                                                       |
| 意图识别     | `/api/component/intent-recognition/deep-recognize` | POST，`body`：`problem_model`                               | 统一外壳：`code` + `info` + `data`（`understanding`、`pass`、`intent`，及 **`details`** 见正文）              |
| 上下文与指标 | answer-generation                                  | HTTP（intent_output）                                       | answer                                                                                                        |
| SQL 转换     | text2sql                                           | 工具调用                                                    | 单字符串（推理 + SQL）                                                                                        |
| SQL 执行     | sql_execute                                        | 工具调用                                                    | JSON 字符串 → result[]                                                                                        |
| 图表         | chart-generator                                    | HTTP                                                        | chartData + analysis                                                                                          |

 

 
