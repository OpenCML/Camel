# 问题：文档是否取代 `defun.cpp:8-10` 那句「函数值已经是标签 + 环境」

## 现象

两处说法互斥，且后者晚于前者：

- **代码**（`src/passes/opt/defun/defun.cpp:8-10`，2026-10-29 14:42 合入 develop）：
  「A function value in this graph model is already a tag with an environment: a FUNC node
  names the function (the tag) and a FILL of kind FunctionClosure adds the values it captures
  (the environment)」——所以这个 pass 只剩「把分派变静态」，手段是**按 (callee, function) 复制一份
  被调者**（`defun.cpp:17-24`），并且它登记为一个**可选遍**（`src/execute/pass.cpp:549`），
  `std::opt` 不跑它。
- **文档**（作者的设计笔记，commit `df3796086`，2026-10-29 22:22；**该文件尚未合入 develop**，本车道读的是提交对象）：
  §1 说标签必须在**值的类型里**——每个函数类型一个 `enum`、每个被取作值的函数一个变体、捕获是
  变体字段；`FUNC` 节点 + `FILL(FunctionClosure)` **不是**这个表示。§4.1–4.3 给出顺序（先定变体
  集 → 整程序换类型 → 换节点）。§4.4 明说按调用点特化**不是**等价替代（「copies only multiply」），
  而且去函数化必须是流水线的**固定一步**，不是可选优化。

## 两条路的代价

**A. 文档为准**（我建议）：

- 收益：函数值彻底不是闭包；调用点是有限标签上的分派；内联/自动微分/后端都不再处理任意闭包；
  验收「`with_grad` 程序在 `std::opt` 之后闭包数与间接调用数都为 0」才有意义。
- 代价：现有 pass 的机制（复制）与状态（可选遍）都要改；`docs/cli.md:298` 那段描述要重写；
  `test/plans/feat/opt/defun.plan.toml` 的 7 个用例期望要跟着改（这是「改期望迁就红」的边界，
  需要你/作者点头）；`std::opt` 自身会做特化复制（`defun.higher_order.counts` 里 15 direct / 4
  indirect 就是复制后的读数），所以 **0/0 的验收必须先把 `std::opt` 的复制关掉或另开口径**——
  换句话说，**现在的 0 闭包是复制换来的，0/0 不等于去函数化成功**。

**B. 代码为准**：把文档 §1/§4 改写成「FUNC = 标签、FILL = 环境」的世界观。代价是作者刚写的
裁决作废，且 `higher_order` 类程序用现机制永远到不了 0/0（除非关掉复制）；收益是不动已合入的
pass 与 7 个用例。

## 默认建议

**文档为准，但分两步落**，第一步已经在做、且不需要改任何旧期望：

1. 新增 `std::defun_value`（不捕获 + 每个函数族只有一个函数）：造单变体 `enum` → 整程序换类型
   （`retype`）→ 把值换 `EnumMake(tag)`、把 `CALL` 换直接 `FUNC`，**类型与节点同一笔提交**
   （`retype.h` 与提交校验器都要求如此，我实测过中间态会被校验器拒绝）。老 pass 一行不改。
2. 等这句裁定下来，再决定老 `std::defunctionalize` 是退役、还是改名降级为「优化」。

## 需要作者一句话

- 老 `std::defunctionalize` 是否退役？（若保留，「去函数化」这个名字归谁？）
- 闭包（`FILL(FunctionClosure)`）在最终实现里是否允许存在？文档 §1 说标签在类型里，那捕获应当
  是变体字段；我按此理解做第一步，未动闭包。