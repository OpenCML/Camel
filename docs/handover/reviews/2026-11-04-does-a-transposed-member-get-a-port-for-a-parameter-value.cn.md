# 转置成员要不要给"一个参数自己的值"留一个端口？

**状态**：待裁；我方默认见第 3 节（**甲：留，且只给"律真的读了值的那些参数"留**）。

写这页的直接原因：`test/plans/feat/compile/transforms.plan.toml#transpose_loop_escape.two_values_of_two_kinds`
要的数是 **29**，而 29 是 `15 + 7*2x` —— 它是 `x` 的**函数**，不是余切单独的函数据。今天转置成员只收到
余切（`type=(float) => (float,)`），所以这个数**没有任何成员能算出来**，除非把 `x = 1` 编进去。这不是缺一条
律，是**答案要的东西不在签名里**。同一堵墙已经在另外 5 条用例上按"闭包成员"记过账
（`docs/handover/questions/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md`，作者 19:55 裁乙）；
这一条是它的另一个来源：要读的值是**参与微分的那个参数本身**，不是闭包成员。

## 1. 读数（实测：树、时间、负载）

树 `D:\Projects\Camel-wt\loopescape` @ `34b659107`（实时 `origin/develop`），2026-11-04 05:0x +08:00，
宿主 88zjwei（32 核）1 m load **13.16 / 18.09 / 20.21**（满载区间，数字只用于判形态，不用于比性能）。
三份探针在 `/tmp/`（不入 git）。

1. **最小形状也拒绝，说明它不是循环的问题**：`g(x) = x * x` 上 `transpose(g)` 读
   `TRANSPOSE function=g skip=cotangent-needs-a-value uri=:op/mul_d position=0 reason=the rule's right
   side reads a value the caller does not carry`，错误串是
   `transpose has no transpose for this function: a parameter holds no cotangent: nothing uses its
   value.` 前后还印着 `resolved=... transposed=(float) => (float,) graph=(float) => (float,)
   same=yes` —— **成员只有一口，而且没有"值端口"这一格**。
2. **要的那条用例已经印出它要的读数，只差最后那个数**：`transpose_loop_escape_reads_outer.cml` 今天读
   `TRANSPOSE function=read_outer carried=2 node=12 origin=0 captures=2 answer=per-target` 与两条
   `ESCAPE ... kindOfTarget=1` / `kindOfTarget=11`，然后停在 `skip=cotangent-needs-a-value
   uri=:op/mul_d position=0`。用例 pin 的**前三条**（`carried=2`、`answer=per-target`、两条 ESCAPE）**全中**，
   差的只有 `29.0`。所以缺的不是转置/逃逸那一层，是 `k = x*x` 这一步要 `x` 的值。
3. **把值传进去今天过不了类型检查**：把调用点写成 `back(1.0, 1.0)`（用例自己的注释第 13 行写的就是
   `transpose(read_outer)(1, 1)`），语义层直接拒：
   `error[SemanticDiag::ArgumentsMismatch]: Function of type '(float) => (float,)' cannot be called with
   argument types: <> (float, float).`
4. **声明早就写了值端口**：`stdlib/types.cml#Transpose` 是
   `(..ParamsOf<F>, ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`，并且带一行
   `@example is_same::<Transpose<(x: float) => float>, (x: float, ct: float) => (float,)>()  // true`。
   同一段注释把差别写成"类型束拼接丢了重数……这个 build 是那一侧要动"。
5. **另一条路也堵在同一个缺口上，但堵点不在我的可改面**：`split(jvp(read_outer))` 读
   `REINTERP split function=jvp_of_read_outer skip=forward construct=a tangent port uri=-
   why=the value half reads the tangent of parameter 0, which the forward half does not take`。
   原因是：`jvp` 造的**值**是规则对节点的一个投影（`Accs 0`），而那个节点的输入里带着切线 —— 值半边要复制它
   就得拿切线。要改只能改 `modules/transforms/jvp.cpp`（本车道禁改）。
6. **面有多大，量过**：`test/cases/**/*.cml` 里 `transpose(` 的调用点 **98** 处；`type=(float) =>
   (float,)` 的 pin **4** 处；今天以 `skip=cotangent-needs-a-value` 记 `known_fail_msg` 的用例 **6** 条
   （5 条是闭包成员，1 条就是本页这条）。

## 2. 三条路

| 路 | 做法 | 代价（量出来的） |
| --- | --- | --- |
| **甲：留端口，只给律真的读了值的参数留** | `buildTransposeMember` 的 `forward` 对"参与微分但值被读到"的参数端口造一个**值端口**，`needs` 通道（今天空转，见 `transpose.cpp#buildTransposeMember` 的"Nothing records 'this member wants a value' any more"）重新记账；`transposeSignature` 按这份账写端口，**算子解析时的返回类型也必须按同一份账写**（否则回到 `same=no` 与 `Runtime indirect call norm-arity mismatch`） | 改 1 条用例（`two_values_of_two_kinds` 的调用点改成 `back(1.0, 1.0)`，答案 29 就是它 pin 的 29）；**不动**那 5 条闭包成员用例（它们按作者 19:55 的裁决走 `forward` 处理闭包端口，与本路无关）。真正的代价在**顺序**：账要在"算子返回类型"之前算出来，而今天的发现是 `buildTransposeMember` 里跑一次 probe 才有 —— 要么算子解析期就建成员，要么把发现挪成一次前向预扫 |
| **乙：不留端口，写死"转置成员只要余切"** | 就是今天的行为，把"为什么"写成规则而不是兜底 | 代价 0 行代码；`two_values_of_two_kinds` 的 29 **永远要不到**，只能一直带 `known_fail`（本车道已经这么记，`known_fail_msg` 钉住今天的拒绝） |
| **丙：让调用点自己把值写进闭包**（作者 2026-11-02 11:30 的原话："A fixed value a body needs is not a parameter: it is captured by the function that is transposed"） | 程序改写成 `transpose((ct) => ...)` 的拼法，值从闭包进来 | **对这条形状不成立**：要读的值就是**被微分的那个参数**，调用点手里只有余切（`main` 里除了 `1.0` 没有别的数）。丙能解 `read_one` 一类的"值来自别处"，解不了本页这条 |

**甲若按声明里的 `..ParamsOf<F>`（全部参数都留端口）落地**，代价换一档：98 个调用点的 arity 全变，
`type=(float) => (float,)` 的 4 处 pin 全要改，`read_one` 这类"值没被读到"的函数会白拿一个端口。所以本文
的甲限定成"只给真正被读到的参数留"，而这一句需要作者顺带裁一下：**它和 `Transpose<F>` 声明里的
`..ParamsOf<F>` 不是同一个形状**，是继续让声明近似，还是等类型束拼接能写出"调用点仍要传的那些值"再对齐。

## 3. 我方默认

**甲（只给被读到的参数留端口）。** 判据是第 1 节第 2 条：用例要的读数已经中了两条半，差的只有最后那个数，
而这个数**必须**从值算出来（`15 + 7*2x`）；乙把这条用例永久钉成 `known_fail`，丙在这条形状上不成立。
第 1 节第 3 条同时说明：今天的 `(float) => (float,)` **不是**声明的形状，而声明已经写了 `(x, ct) => (float,)`
并把差别记成缺陷 —— 甲是把这个缺陷补上，不是新开一条语义。

阈值写死：**若落地时发现除 `two_values_of_two_kinds` 之外还要改超过 20 条用例的期望，就改乙**，并在
`stdlib/types.cml#Transpose` 的注释里写清"转置成员只收余切，值由闭包/残差负责"。

## 要作者答的一句

**「一个转置成员在律要读某个**参与微分**的参数自己的值时，应不应该有一个值端口（`(x, ct) => (ct,)`，
即 `Transpose<F>` 声明里已经写的形状）？我默认『甲：留，且只给真的被读到的参数留』；若留，请同时裁一句：
`Transpose<F>` 的声明是继续写 `..ParamsOf<F>`（近似），还是等类型束拼接补上」