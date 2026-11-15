# `var` 迁移面：75 / 281 重数、A–F 分类与机械边界

**结论：566 个 `var` 绑定里只有 477 个（84%）是纯机械改写；剩下 89 个要人判断——39 个是闭包写外层 `var`（`test/`），46 个是模块顶层 `var`（`test/`，且被一条尚未落地的裁定卡住），4 个是 `stdlib/stream.cml#tee` 的局部（改 `Ref`）。`stdlib/perf.cml#timeit` 已经迁完，不用再做。**

- 基线 `origin/develop`，工作树 `D:\Projects\Camel-wt\varsurvey`，分支 `lane-varsurvey`，tip `faccf38b97dc15e2309995d798a7d3ca4a61f0a0`。
- 只读车道：没改任何 `.cml`、没改 plan、没改 `src/`，`git status` 里只有本页一个新文件。
- 文中行号都是 tip 上的行号；所有数字都可在本树用第 4 节的命令复核。

---

## 0. 口径：一个 `var` 记号 ≠ 一个绑定

三件事必须先分开，否则 75 / 281 这种数字没法对齐：

| 口径 | `test/` | `stdlib/` |
|---|---|---|
| 出现 `var` 这个词（含注释、字符串）的文件 | **317** | 12 |
| 同一个口径的记号数 | **834** | 27 |
| 去掉注释与字符串后代码里的 `var` 记号 | **582** | 27 |
| 其中**不绑定名字**的出现（`uses var fs` 子句、`` var `x` `` 这种普通名字） | 22 | 23 |
| 剩下的**声明记号** | 560 | 4 |
| 这些声明记号绑定的**名字**（`var a, b = 1, 2` 一个记号绑两个） | **562** | **4** |

`562 + 4 = 566` 个绑定，`22 + 23 = 45` 个不绑定出现，`566 − 2 + 45 = 609 = 582 + 27`。
下文所有「次数」都指**绑定数**，不是记号数。

### 作者的 75 / 281 对不上，建议改用 91 / 284

- `var` 形参：`git grep -l -E '[(,]\s*var\s+\w+'` 给 **83** 个文件（含 `_ = (var int)?` 这类注释行时是 84）；加上 `with <var …>` 上下文参数的 **9** 个文件，并集 **91** 个文件、**131** 处绑定。这是 75 的替换值。
- 声明：`git grep -l -E '\bvar\s+\w+\s*[=:]'` 给 **284** 个文件（局部 + 顶层；`export var count = 0` 这种有 `export` 前缀的也算）。这是 281 的替换值。
- 上一轮 `2026-11-15-var-is-a-name-scope.cn.md` §5.1 已经给出同样的结论（83 / 301），并且写明「用任何一条单一、可写下来的口径都复现不出 75 / 281」。本页在其基础上把「声明」拆到了绑定级，并重数了闭包那一类。

---

## 1. A–G 分类表

`test/` 一侧（562 个绑定）：

| 类 | 含义（判据） | 文件数 | 次数 | 机械 / 人工 |
|---|---|---|---|---|
| **A** | `var` 形参（`var x: T` 在参数表里） | **91** | **131** | 机械 |
| **B** | 局部 `var`，只在直线代码里被写（或从不被写） | **197** | **306** | 机械 |
| **C** | 局部 `var`，在分支臂里被赋值 | **21** | **25** | 机械 |
| **D** | 局部 `var`，在循环体里被赋值 | **10** | **15** | 机械 |
| **E** | 闭包（lambda 或嵌套 `func`）写外层 `var` | **30** | **39** | **人工** |
| **G** | 模块顶层 `var`（不在 A–E 里，见下） | **37** | **46** | **人工（且被裁定卡住）** |

`stdlib/` 一侧（4 个绑定）：

| 类 | 含义 | 文件数 | 次数 | 机械 / 人工 |
|---|---|---|---|---|
| **F** | `stdlib` 里的 `var` 声明 | **1** | **4** | 人工（作者已定：`Ref`） |

**闭合算式**：`562 + 4 = 566`；`131 + 306 + 25 + 15 + 39 + 46 + 4 = 566`。✔

A 的内部：函数形参 120（83 个文件）、lambda 形参 1（`tools/grammar/functions.cml:58`）、`with <var>` 上下文参数 10（9 个文件）。
121 个可调用形参里 **109 个在体内被写**；13 处在体内的分支里被写，**0 处在循环体里被写**，**5 处是被闭包写**（这 5 处同时属 E 的形状，见 §2.5）。

B 的内部：53 处从不被重新绑定，253 处只有直线写。
E 的内部：36 处是闭包直接赋值，3 处只经「把外层 `var` 当实参交给 `var` 形参」写回。
E 的 39 处按闭包形态：**7 处**写在 `foreach` / `map` 的元素闭包里（改 `fold`）、**5 处**写在嵌套 `func` 里、**27 处**写在绑定成名字或当实参传出的 lambda 里（改 `Ref`，或改成返回值 / 循环状态）。
G 的内部：39 处被写、7 处只读。

**关于纪律那一条**：E 抢占优先级，所以 C/D 里没有重复计数。反向核对：39 处 E 里有 **1 处**同时也在分支里被写（归 E，`captured_write_graph.cml:57` 形状），**0 处**同时也在循环体里被写——闭包本身就替掉了那个循环。C 里 0 处同时在循环里被写，D 里 0 处同时在分支里被写。分类互斥且闭合。

---

## 2. 每一类：机械还是人工，为什么

### 2.1 A（`var` 形参，131 处 / 91 文件）—— 机械

语义已经定死：「传入值、传回新值」，等价于 `x = f'(x)`（设计原则 3.5）。所以改写是纯签名手术：形参从参数表里拿掉、值照旧传、最终值挂到返回值后面；调用点按位置写回。**不改期望输出**——这正是 3.5 已经承诺的语义，今天的行为本来就该是这样。

工作量的形状是「宽而不深」：109 个被写的形参要加返回值，120 个函数签名 + 全部调用点（含 `->` 管道、`<with>` 显式实参、函数值间接调用）。要留神的是函数值：`let f = bump; f(g)` 这种调用点在编译期看不到 `bump` 的签名时，写回要不要按类型接受，需要一次判定（`test/cases/linear/byref/var_param_jit.cml`、`var_function_as_value_function.cml` 就是钉这个的）。**这一类可以整体交子代理**。

### 2.2 B（直线局部，306 处 / 197 文件）—— 机械，且大半已经做完

建图器本来就有「不逃逸的 `var` 化成 SSA」的路（`Builder::collapseNonEscapingVariableReads`，见 `2026-11-15-var-to-ssa-start.cn.md` §2）。B 类是它已经吃下的形状：删掉 `:var/new` 与 `:var/get`，读接最近一次写的值。**期望输出不变**，图会少节点（任何按节点序号断言的 golden 要重录，那是另一件事）。
53 处从不被重新绑定——连删都不需要删，直接就是 `let`。

### 2.3 C（分支里写，25 处 / 21 文件）—— 机械

裁定给的目标形状：`if c then { x = 1 }` 即 `x = if c then 1 else x`。臂里的写成为 JOIN 的值，读接 JOIN。**期望输出不变**；这正是 `test/cases/compile/var/write_in_branch.cml` 与 `test/cases/trans/cml/arm_write.cml` 钉的东西。
唯一要判一下的是「没有 `else` 的 `if`」：`if c then { x = 1 }` 的另一条臂给的是 `x` 自己，机械可写，但生成器要真的把「臂前版本」当 else 值（不是漏掉）。

### 2.4 D（循环体里写，15 处 / 10 文件）—— 机械

目标形状：写成为 LOOP 的携带状态，循环后的读读 LOOP 的 state 输出。**期望输出不变**（`test/cases/compile/var/write_in_loop.cml`：三轮加一，读 3）。
注意两件事，都不是判断而是接口：循环本身是表达式，携带状态要和循环自己的结果一起排（`let _ = loop …` 那些用例已经把结果丢掉，最容易）；以及 `:var/peek` 那类「只喂不保留的读」在 `peek_loop.cml` 里仍然是合法的。

### 2.5 E（闭包写外层 `var`，39 处 / 30 文件）—— **人工判断**

这是唯一「改法不止一种、且可能改期望输出」的一类，所以不能整类交子代理。按作者给的三条出路分：

- **改 `fold`（7 处，3 个文件）**：`(0..n)->foreach<(i) => sync { out = out->append<i> }>` 这种。改成 `loop` 的状态或 `fold`，**输出一般不变**，机械度高，但要不要保留原来「每轮都能看到上一步」的语义要人看一眼。
- **改返回值 / 循环状态（27 处 lambda 里的一部分 + 5 处嵌套 `func`）**：闭包只在本函数内被调用，把状态改成参数进、结果出。**输出不变**（`ok_capture_local_var{,_as_value}.cml`、`fib2.cml` 的 `cnt` 都是这种），但闭包一旦有两条读路径（一个读外层、一个读捕获），改写是重构而不是替换。
- **改 `Ref`（剩下的）**：闭包离开这次调用（返回、存进结构体字段 / 数组、装进另一个闭包、交给会保留它的被调函数）。**输出可以不变**（`Ref` 是语义性的），但类型要跟着改（`Ref<T>` 出现在签名里），调用点要造盒子。

**已经出错的 5 个**（`compile/state_escape/err_escape_*`）现在就是编译错（`ClosureCaptureEscapes`，0x03000210），它们不产出 stdout，改的是**诊断文本**：新规则不再有「单元逃逸」这回事，消息换成「闭包不能给外面的名字赋值」+ 三种改法。

**依赖关系**：A 里那 5 处「形参被闭包写」必须先做 E（否则形参的写回语义跟着闭包一起变），所以落地顺序应该是 **E → A → C/D → 删单元**，和作者的 a→b→c→d 一致；只是 a 里那 7 处 `foreach` 可以独立先做。

### 2.6 G（模块顶层 `var`，46 处 / 37 文件）—— **人工，而且被裁定卡住**

这一类不在作者给的 A–F 里，但它是「281 个声明 `var` 的文件」里最大的一块之一，而且**它决定了迁移能不能收尾**：

- 3.5 新条款说「模块顶层只有声明……顶层的 `var` 没有用处：编译错误」，但 `test/plans/feat/compile/state_escape.plan.toml` 开头明说这半条**不在那一批**落地：「until then a module `var` works as it did -- the one way out this plan does not refuse」，并说加上拒绝会「246 plan entries go red」、要连带迁 35 个用例和 2 个文档例子，还指向一页 `docs/handover/questions/2026-11-14-who-migrates-the-module-level-var.cn.md`——**那一页在树里不存在**（`docs/handover/questions/` 只有 `README.md`）。
- 形状上这 46 处不是「局部状态」：`counter.cml`、`counter_lib.cml`（`export var count`）是**跨模块共享状态**，`captured_write_value.cml` 的 `p/q/n/m` 是四个函数共用的累加器。改成 `Ref` 是最直接的（`Ref` 是句柄、可以到处传），但有些用例的语义就是「顶层单元被 import 的所有模块看见」，改成 `Ref` 之后**输出可能变**（谁先写、`import` 的求值顺序）。
- 结论：G **不能整类机械**；而且它是 37 个文件的期望输出会变成编译错的那一批，必须先有裁定。

### 2.7 F（`stdlib`，4 处 / 1 文件）—— 人工，但已经定了

- `stdlib/stream.cml:181` `tee`：4 个局部 `var`（`:187 src`、`:188 buffer`、`:189 taken`、`:190 peak`），被 3 个嵌套 `func`（`pull:194`、`read:211`，另有 `side0`/`side1`）写（`:198`、`:199`、`:214`、`:219`）。形状是「一次调用里几个闭包共享一个缓冲」→ 作者已定改 `Ref`。**输出不变**，但是 `Ref` 进签名、4 个局部换盒子，是 `stdlib` 里唯一要动的地方。
- `stdlib/perf.cml#timeit`：**已经迁完了**。`:14` 起是 `with <repeat: int> export func timeit(...): float[]`，返回 `concat([duration], rest)`，`:9-10` 的注释明说「The answer is a value now, so nothing is carried」。上面那条「改返回值」不再需要做。
- `stdlib` 另外 8 个文件（`ckpt`、`graph`、`process`、`random`、`state`、`signatures/*`、`test_support`、`tiles`）出现的 23 处 `var` 全是 `uses var <资源>` 子句或 `var` 当参数名（`stdlib/signatures/nn.cml:91` 的 `var: f32[...]`），**不是绑定，不用迁**。

---

## 3. 期望输出会变的用例（按文件名）

这里只列**字节会变**的；「形状要改但输出能保持不变」的不列（那些属于 §2 的机械档）。

1. `test/cases/linear/byref/var_capture_reference.cml` —— 整份文件的期望输出就是「按引用捕获」：`if` 臂里写 5、lambda 写后 `12 12`、`foreach` 写 6、嵌套 `func` 写 8、`var` 形参捕获写 21。改成按值捕获后这些数全部变（或变成编译错）。
2. `test/cases/compile/stream/generators.cml` —— `reading()` 的注释钉的正是「闭包看到 later pull 写的 5，不是 0」；按值捕获后 `read()` 是 0，`main` 最后一行从 `5` 变 `0`。
3. `test/cases/linear/byref/var_alias_captured.cml` —— `let f = () => v[0]` 之后 `v[0] = 9`，`f()` 现在印 9；按值捕获（写复制）后印 1，`big[0]`/`v[0]` 仍是 `1`/`9`，只有 `f()` 那一个数变。
4. `test/cases/linear/toplevel_var/argument_order.cml` —— 每一行都是「读与写交错」的次序断言（`100 110`、`120 120`、`250`、`1 2`、`10 11`），依赖的可变单元一旦去掉，再加上顶层 `var` 本身要被拒，整份期望输出重写。
5. `test/cases/linear/byref/captured_write_value.cml` —— 期望输出（`7110`、`12`、`54`、`95`）钉的是「调用值 = 返回值 + 每个被写捕获的新版本」这条已删除的约定（`docs/design/effects/edges.cn.md` 5.6），机制没了这些数就没有意义。
6. `test/cases/linear/byref/captured_write_order.cml`、`captured_write_graph.cml` —— 同上，`cnt/levels/calls` 与 `a/b` 的版本序列按「写后读拿到新版本」算；闭包不再是单元，读改接 SSA 值，行序与数都可能变。
7. `test/cases/compile/function_values/captured_var_in_two_arms.cml` —— `bump(v)` 写捕获的 `v`，两个臂随后都读 `v`，四个函数各自期望 `2`；按值捕获后闭包看不到 `bump` 的写（或直接编译错）。
8. `test/cases/modules/tensor/var_tensor_reference.cml` —— 第三段「lambda 把 `w` 换成两倍」期望 `20 20`；闭包写不进外层名字，这行要么变要么错。
9. `test/cases/effects/writer_table_through_callee.cml` —— 期望 `0` 然后 `41`（回调写捕获的 `counter`）；闭包不能写外层名字，`41` 这一半变了。
10. `test/cases/effects/callback_write_through_parameter.cml` —— 打印 `localBefore local` 与 `counterBefore counter`，四个数里后两个是回调写出来的；同一类。
11. `test/cases/linear/nvm/fib2.cml` —— 打印 `cnt`（嵌套 `func` 每递归一次 `+= 1`），是闭包写的累加量；改成返回值后打印的调用次数要么变要么整段重写。
12. `test/cases/compile/nullable_assign.cml` —— `seen` 先由 lambda 写、再由 `foreach` 每轮写，期望 `4.5` 与 `2.0`（最后一次）；闭包写不进外层，两行都要重算。
13. `test/cases/linear/byref/lambda_capture_write.cml` / `lambda_capture_ignored.cml` —— 两份的期望（`1`、`0`）都取决于「捕获的写有没有执行」这条单元语义；前者变，后者靠「调用没被执行」也可能保住 `0`，要逐条看。
14. `test/cases/resources/effects.cml` —— 末尾 `println(c)` 是 `2 + bump + inc` 之后的值（`inc` 是闭包 `c += 1`），随闭包写消失而变。
15. `test/cases/opt/adb67e081/rc04_seed72*.cml`、`test/cases/opt/e9/rc04_seed72.cml`、`test/cases/opt/inline_cycle/seed23_fold_inline*.cml` —— 这 5 个（7 条 plan 条目）现在钉的是 `ClosureCaptureEscapes` 的**诊断字节**（`return` / 嵌套闭包 / 捕获写），新规则下诊断换成「闭包不能给外面的名字赋值」+ 三种改法，stderr 全变。
16. `test/cases/compile/state_escape/err_escape_{return,field,array,stored_by_callee,into_closure}.cml`、`test/cases/effects/err_capture_escapes.cml`、`err_closure_capture_pure.cml` —— 负例，期望输出是诊断；消息表与 code 一起换。
17. **G 那 37 个文件** —— 顶层 `var` 一旦按 3.5 拒掉，它们从「打印一串数」变成「编译错」，全部要重写。跨模块共享状态的那几个（`linear/toplevel_var/counter*.cml`、`compile/module_imports/d1_ok.cml`、`compile/module_load/recursive_late_var.cml`）还要重新决定「状态放谁那里」，输出可能真的变。

**不会变、但形状必须改**（只是提醒别把这些算进上面）：`ok_capture_local_var{,_as_value}.cml`（改成 fold，仍 `3`）、`rebinding_loop.cml`、`peek_loop.cml`、`sort_rebinding.cml`、`fault_in_foreach_closure.cml`、`effect_order_folded.cml`、`write_in_branch.cml`、`write_in_loop.cml`、`captured_var_in_two_arms` 之外的 `C`/`D` 用例。

---

## 4. 复现命令

**总数（这两条能完全复现）**

```powershell
git -C D:\Projects\Camel-wt\varsurvey grep -l '\bvar\b' -- 'test/**/*.cml' | Measure-Object      # 317 个文件
git -C D:\Projects\Camel-wt\varsurvey grep -o '\bvar\b' -- 'test/**/*.cml' | Measure-Object      # 834 个记号（含注释/字符串）
git -C D:\Projects\Camel-wt\varsurvey grep -l '\bvar\b' -- 'stdlib/*.cml'                          # 12 个文件
```

「去注释后 582」在 `git grep` 里没有直接等价的写法（注释里 `var` 太多）；用本车道的词法扫描器（`%TEMP%\varsurvey\analyze3.js`，只读文件）得到 582 / 27。它对 `var` 的判定是：token 是 `var`、下一 token 是标识符、再下一 token 是 `=` 或 `:`（`uses var fs` 与 `var` 当普通名字都不算）。

**A（形参）**

```powershell
git -C D:\Projects\Camel-wt\varsurvey grep -l -E '[(,]\s*var\s+\w+' -- 'test/**/*.cml'          # 83 个文件 / 122 个记号
git -C D:\Projects\Camel-wt\varsurvey grep -l -E 'with\s*<[^>]*\bvar\s+\w+' -- 'test/**/*.cml'  # 9 个文件（11 行，其中 1 行是文档注释）
# 并集 91（唯一的交集文件是 linear/byref/var_param_reference.cml，两样都有）
```

**声明（B+C+D+E+G 的文件全集）**

```powershell
git -C D:\Projects\Camel-wt\varsurvey grep -l -E '\bvar\s+\w+\s*[=:]' -- 'test/**/*.cml'        # 284 个文件
```

**B–E 的判据不是 grep 能判的**（要判「这次写落在哪个块里」）。规则写死在这里，逐条可读验证：

- 写 = `名 =`、`名.f =`、`名[i] =`、`名 op= e`，或把 `名` 交给一个 `var` 形参（按 `func` 声明表解析，`let f = bump; f(x)` 这种别名解一层）。
- 上下文 = 从这次写往外走，最近的 `{` 链上有没有 `branch`（`then`/`else`/`case … =>` 的块）或 `loop`（`do` 的块）。
- 闭包 = lambda 的 `=>` 块或**嵌套 `func` 的体**（`static for … =>` 是编译期展开，**不算**闭包；`match` 的 `case … =>` 也不算）。
- 优先级：闭包 > 循环 > 分支 > 直线。

**每类三个例子（grep 能直接打印出声明行）**

```powershell
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var w = ones'          -- test/cases/compile/array_alias/var_widening.cml        # B: 11
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var c = 0'             -- test/cases/linear/byref/var_param_reference.cml:87     # B
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var acc: int\[\] = \[\]' -- test/cases/consistency/new_values/append_new.cml    # B: 19
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var x = 0'             -- test/cases/compile/var/write_in_branch.cml             # C: 17
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var t = 3'             -- test/cases/trans/cml/arm_write.cml                     # C: 7
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var c = 1'             -- test/cases/linear/byref/captured_write_graph.cml:57    # C
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var x = 0'             -- test/cases/compile/var/write_in_loop.cml              # D: 15
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var z = 0'             -- test/cases/linear/runtime/var_read_before_region.cml   # D: 18
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var total = 0'         -- test/cases/consistency/faults/fault_in_foreach_closure.cml # D: 9
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var out: int\[\]'      -- test/cases/consistency/new_values/rebinding_loop.cml   # E: 12
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var c = 0'             -- test/cases/linear/byref/var_capture_reference.cml:57   # E
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var cnt = 0'           -- test/cases/linear/nvm/fib2.cml                         # E: 8
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var cnt'               -- test/cases/linear/toplevel_var/counter.cml             # G: 9
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var p = 0'             -- test/cases/linear/byref/captured_write_value.cml       # G: 23
git -C D:\Projects\Camel-wt\varsurvey grep -n 'export var count'      -- test/cases/linear/toplevel_var/counter_lib.cml         # G: 4
git -C D:\Projects\Camel-wt\varsurvey grep -n 'var '                  -- stdlib/stream.cml | Select-Object -First 4            # F: 187-190
```

---

## 5. 已经迁完的（别重做）

- `stdlib/perf.cml#timeit`：返回值形态，已落地（本页 §2.7）。
- 12 个用例在 `96d8c9ea3`（C1）里改完，84 次比较 82 次逐字节相同。
- 下列文件已经从 E 里消失、现在只剩直线写（上一轮报告把它们列在 E 里，本页重数后不算）：`compile/state_escape/ok_capture_within.cml`、`consistency/values/closure_var.cml`、`consistency/entering/assignment.cml`、`trans/cpp/exec/variables.cml`、`tools/grammar/functions.cml` 的 `count`。

## 6. 这篇数的限制

- **函数值间接调用**：`let f = bump; f(x)` 我解了一层别名；再深（放进结构体 / 数组 / 返回）就看不见了。所以 A 的 109 是下界，E 的 39 也可能少 1–2 处（`var_param_jit.cml` 这类）。
- **内联 lambda 体**（`=> expr` 不写块）里没有赋值，语料里一处都没有，所以按块判闭包不漏。
- **`static for`**：7 处曾被误判成 E（`compile/static_expansion/*`、`parse/rulings/static_for_head_names.cml`），已按「编译期展开不是闭包」剔掉；它们在编译后是直线代码，归 B。
- **G 的数字**只算「绑定」，不算 `uses var` 子句；后者在本树 17 个文件里出现（`test/cases/effects/*` 为主），与迁移无关。