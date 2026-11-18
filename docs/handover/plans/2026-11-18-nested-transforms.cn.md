# 变换的嵌套（施工计划）

> 决策者 2026-11-18，负责人同日批准开工（"抓紧开发吧"）。设计：`docs/design/program-transforms/nested-transforms.cn.md` 第 2 节。排期：与"提交检查效应与冲突、看不到写法"（`plans/2026-11-18-commit-checks-and-spelling.cn.md`）并列最高，**单开车道并行**，两件动的文件不重叠（这件在 `modules/transforms/`、`stdlib/vjp.cml`、规则集；那件在提交检查与 `stdlib/graph.cml`）；生成器状态下移照常在另一条车道。运行时分派（设计稿附录 A.2）**不做**：调用的函数在变换时未知，一律在编译期拒绝，诊断写明原因。

## 甲：库的形状（设计稿 2.1，先做）

1. `split` 交出两张静态的图：前向 `fwd: x ↦ (y, 残差)`，线性一半 `lin: (残差, 切向量) ↦ 切向量`；残差是显式参数，不再是闭包捕获的运行时值。旧形状删除，用到它的地方（`vjp`、`grad`、`has_aux`、导出、测试金标）一并迁移。
2. `transpose` 作用在静态的 `lin` 上，得到 `bwd: (残差, 余切) ↦ 余切`。
3. `stdlib/vjp.cml`：`vjp(f)`、`grad(f)` 在求值时算出 `fwd`、`bwd`，交出的函数只调用它们（`grad(f)` 即 `(x) => { let (_, r) = fwd(x); bwd(r, 1.0) }`，`with` 值照今天的规则传给 `fwd`）。
4. 读数：训练案例（`examples/case-study/train.cml`）每步不再调用 `split`、`transpose`（`--trace transforms` 读出变换次数：只在 `grad(f)` 求值时各一次）；六计划逐名比较。

## 乙：变换递归进闭包（设计稿 2.2）

1. `reinterp`（`jvp`）、`split`、`transpose`、`vmap` 遇到闭包（函数体里的 lambda、捕获了函数值的闭包）时：代码已知就递归变换，捕获的数值照常提升，捕获的函数值随之变换；同一被调函数配同一规则集只生成一份。
2. 删除按来历的拒绝（"值从元组里读出""值是某次调用的返回值"）；判据只有一个：变换执行时，被调函数的代码是否已知。不知道就拒绝，诊断写"调用的函数在变换时未知"并给出位置。
3. 高阶函数在调用处按常量函数实参特化（`map(f, xs)` 中 `f` 是 lambda 或具名函数），或按类型提升函数形参，两者都可，择简单的先做，报告里写明选了哪个、为什么。

## 丙：规则互相覆盖（设计稿 2.3）

1. 一条自动检查：`stdlib/rulesets/transpose.cml` 能产出的每个算子，`stdlib/rulesets/jvp.cml` 里都有前向规则；缺的补上。检查进快层。

## 验收（命令与原始输出写进 `docs/handover/reports/2026-11-18-nested-transforms.cn.md`）

每条一个用例，四个后端一致：
- `grad(grad(f))`，`f(x) = x³` 在 2 处得 12（`second_order_refused_vjp` 那两条改为钉这个数值）；
- Hessian（`f(x, y)` 的二阶偏导矩阵）；
- `jvp(vjp(f))`；
- 元学习：`L(θ − α∇L(θ))` 对 θ 求导，与有限差分在 1e-4 内一致；
- `vmap(grad(f))` 逐样本梯度；
- `map(f, xs)` 求导，`f` 分别为 lambda、具名函数、捕获了 θ 的闭包；
- 扰动混淆的经典例子：`d/dx (x * (d/dy (x + y)) 在 y=1 处) 在 x=1 处` 得 `1`；
- 拒绝：调用的函数在变换时未知的程序，编译期报出诊断原文。

完成后接着做：两套求导收成一套（原生 `autodiff.grad` 的用例全部由规则组合的 `grad` 通过后，删除原生实现），单独一笔。
