# 代码审查总索引

十五份报告（`docs/handover/reports/code-review-*.md`）加上"资源度量"一节（只在 `docs/handover/reviews/questions-2026-10-24-evening.md` 里，无独立报告）。这份索引不重复细节，只做三件事：按严重程度分层、把"一个改动能解决的一组条目"并在一起、给出审查里反复被违反的规则。

**缩写**：AD 自动微分、BE 执行后端、FE 前端、GE 泛型、IX 下标与切片、MG 宏/模块/GC、ME 内存、OT ONNX 与 Tiles、PA 优化遍与图存储、PY Python、RW 改写、ST 流、TO 工具、TY 类型系统、WH `when`、RS 资源度量（问题页）。条目号是各报告里的编号（`AD/H3` = 自动微分报告的 H3；`FE/A1` = 前端报告 A 节第 1 条；`OT/O2`、`OT/T1` 分别是 ONNX 与 Tiles 两部分）。

**怎么用**：先看第五节的"改动组"（C1…C26）——协调者排的是改动，不是症状。第一至四节的每个条目都标了它属于哪个组；"先测"一栏标 **T** 表示报告自己写明或我读代码后认为需要先写用例确认再改（失败场景是推出来的，不是跑出来的）。

---

## 一、第一层：静默地算错结果，或值语义被破坏

这一层的共同点：程序不崩、不报错，给出另一个答案。

| 组 | 一句话 | 位置 | 先测 |
|---|---|---|---|
| C1 | 丢弃/删除/内联/调度一个"会出故障的纯节点"：`(a/b)*0 => 0`、`let r = 5/z` 被 DCE 丢掉，故障消失 | RW/A1，PA/A1，PA/A2 | T（已有场景） |
| C3 | 数组原地写破坏值语义：槽数组 `xs[i]=v`（**ME/1 已修复**，`set_value.cpp`，用例 `linear.var_param_slot_array`；之后"槽数组按实际引用计数判断"仍待做，见 ME/3）；逃逸分析数到 256 就答"不逃逸"；内联数组"唯一持有"无人强制；prl 按偏序执行而计划按线性序推出 | ME/1（已修）、ME/2、ME/3、BE/7 | ME/2、ME/3、BE/7 T |
| C25 | 导出到 Python 的数组是可写别名，改它就改了 Camel 的模块常量；DLPack 导出没有只读标志 | ME/4，PY/P2，PY/P13 | T |
| C5 | ONNX 导出算出的不是运行时的值：标量除零/数值转换丢故障、负下标成"从末尾数"、沿轴归约 f32 累加、`max/min/maximum/relu` 丢 NaN 与 ±0 规则、`softmax/layer_norm/conv` 把 f64 悄悄转 f32、动态形状的 `full/zeros` 元素类型错、`layer_norm` 写死 `axis=-1` | OT/O1–O6，IX/3，IX/4 | O2（CUDA EP 行为）、O6 T |
| C6 | f64 当 f32 读/写：`gelu_grad/erf_grad/layer_norm_grad`；`permute_inverse`/`layer_norm_grad` 的推断写死 f32；`mean_axis` 等混合宽度；`sum_to` 在 f32 累加（违反 7.3） | AD/H3，AD/M6，AD/L1，IX/8，OT/O4，OT/O5 | AD/H3 T（f64 有限差分） |
| C26 | 梯度语义：沿轴 `max/min` 的并列值每个都拿完整 `dy`；规则漏掉某个活跃输入不报；自定义规则的梯度只比元素类型码不比形状；标量 `pow` 拒绝数据指数、标量 `math` 只有 `sqrt` 有规则（后两条是响亮的拒绝，归第二层） | AD/H4，AD/M1，AD/M2 | AD/H4 T |
| C21 | 前端按错的优先级/词法解析：一元 `-`、`!`、`~` 与 `->`、`^`；未闭合字符串吞到文件里最后一个转义引号；`007.5` 与 `007` 不一致 | FE/A1，FE/A2，FE/A4 | |
| C20 | 打印出来的类型含义变了：函数的数组打印成"返回数组的函数"；元素为 `T?`/`T!` 的数组、一元组、`uses #0` 写不出 | TY/T1 | T（往返性质测试） |
| C9 | 编译期求值在猜：`match type` 遇到未绑定类型变量当叶子走 `_`；重载的泛型名取第一个；被调 `when` 在调用方作用域求值；`catch(...)` 之后继续折叠；读非常量名被报成"有效应" | WH/A1–A4，WH/B1–B3，GE/A2 | WH 中标"待验证"的几条 T |
| C8 | 重载选择在泛型/非泛型两条路上规则不同：泛型路径不看 `when`、`with`，字面量处理与实例化阶段不同，于是声明处接受、调用处报歧义，或多加一个重载就让原来能编的调用编不过 | GE/A1，GE/A4，GE/B4 | |
| C11 | 流的线性只检查 `for` 与 `s()`：传参、放进元组、捕获、状态交回、推断出的容器都漏过去；`leaveScope` 在异常路径上跑检查，盖掉真错误 | ST/1–ST/4 | |
| C12 | 承诺静默地不被检查：递归函数量不出来 → `pure` 不拒；量不出来的函数体跳过子句；第 256 个参数的资源使用被丢 | RS/1–RS/3 | T |
| C29 | 改写/事实/规则：`meet/widen/narrow` 存了从不调用；事实缓存键是名字文本；普通规则与泛型规则实例编号相撞（`explain` 错归）；泛型规则静默截到 64 个类型；`check` 的几处"过了"不代表什么；条目共享在 `rewrite` 里丢失 | RW/A2–A6，RW/A9 | RW/A2 T（图里能否成环） |
| C10 | 切片/取行的读法在六处各推一遍"步长 1、掩码 3"；第二套切片与取行；常量 `t[-1]` 在 ONNX 里成最后一个元素；切片的轴接受负数；"常量"后门让 `let k = 2.0` 遇 f32 数组静默收窄 | IX/1–IX/3，IX/5–IX/7 | |
| C4 | 数值转换规则有三张表且和原则 5.1 不一致：`as` 把 `i32→f32`、`int↔bool` 标成安全；`castSlotFrom` 有被 `ASSERT` 挡住的分支；`convertTo` 是第三张，`5 as float` 失败而 `n as float` 通过；`py::cast` 把 `5` 当 `true`、`bytes` 当 `string`、`1e300` 当 f32 `inf` | TY/T4，PY/P7，PY/P5 | TY/T4（`ASSERT` 在 release 的行为）T |
| C22 | 同一个类型不同的回答：`assignableFrom(nullptr)`；`sync`/`with` 常量标记进键却不进 `equals`；元组/结构体/函数/`T?` 没有 `unify`，聚合内的数组分支合并失败；结构体转换规则三处顺序规则不同；`Void` 目标在聚合内也接受任何类型 | TY/T2，TY/T3，TY/T6–TY/T8 | |
| C32 | 工具：接受的选项不起作用；`catch (Diagnostic&)` 之后没有 `return` 导致不打印就退出 0；退出码几乎全是 1，`std::exit(1)` 绕过收尾 | TO/1–TO/3 | |
| C13 | 吞异常、把内部错误变成用户错误：宏求值的 `catch(std::exception)` 把 `logic_error`/`MemoryExhausted` 报成宏失败；`pyToCamel` 的 `catch(...)` 把具体错误换成"转换失败"；`when` 的 `catch(...)`；`isIsolatableFault` 默认"不隔离"（新故障种类静默逃出 `attempt`）；任何 `invalid_argument` 报成 `RuleTwoWay` | MG/R9，PY/P7，WH/A4，BE/5，RW/A7，ST/3 | |
| C16 | 模块解析静默改选：`.cmo` 找到但加载失败会换下一个候选名或同名内建；任意同名主干文件（`foo.md`）按目录遍历顺序被当作模块；条目名 `"main"` | MG/R2，MG/R3，MG/R4 | MG/R2、R3 T |
| C18 | 限制与回退只写日志：prl 遇循环整图退成串行且吞掉真正的图不一致；内联 256 次、32 会话、16 轮静默停；`#gpu` 遇未解析类型/控制边/效应返回空，节点静默留在主机；泛型规则 64 个类型 | BE/3，PA/D，OT/T1，RW/A5 | OT/T1 T（运行期重试是否补上） |
| C14 | 并行 `map` 出故障时"哪些元素执行过"后端之间不同；fvm 两份解释器（Linux 测一份、Windows 用另一份）对未知操作码、帧访问处理不同 | BE/4，BE/1 | BE/4 先用例钉住现状 |

## 二、第二层：崩溃、泄漏、挂起、不可靠的缓存

| 组 | 一句话 | 位置 | 先测 |
|---|---|---|---|
| C24 | GC 与进程级状态：分配器根集合/清扫器是进程单例，每个 `Context` 覆盖、销毁时清空（第二个 Context 丢根，Python 绑定会碰到）；每次建 `Context` 重新注册导数，清掉张量切空间；收集线程等 500 ms 就放弃、不升级，堆上限只是建议；收集中途异常让堆停在半搬动状态且重入被吞；元素视图是不持块的裸指针；`ForeignPinnedHandle` 成败取决于分配位置；延迟释放队列全局锁、终结器里分配、退出时不运行；GC 握手在持锁期间等 500 ms | MG/R1，MG/R5，MG/R8，AD/H1，ME/5–ME/8，BE/6 | MG/R1、R5 T |
| C27 | Python 边界：DLPack 导入在成功前改名，失败泄漏生产者内存并吞掉真错；C++ 异常穿出 `sessionCall` 终止解释器；单个共享 Context，后续 `load()` 的搜索路径被忽略，失败查找一直缓存；GIL 不释放、泛型导出被拒（§10.7）；释放终结器取 GIL 与收集互斥的死锁风险 | PY/P1，PY/P3，PY/P4，PY/P5，PY/P10 | PY/P10 T |
| C28 | 打包：扩展按 CPython 版本各一份（Linux cp311、Windows cp310、macOS 无），3.12/3.13 用户 `import camel` 失败；冒烟测试没有 `--no-index` 可能测到 PyPI 上的 wheel，Windows 冒烟不真调用函数；`site-packages` 路径拼错 | PY/P6，PY/P11，PY/P12 | |
| C19 | 以指针为键或键不对的缓存：`pure.cpp` 缓存会把声明到一半的命名结构体缓存成"不纯"；`Type *` 为键的缓存把只差维度名的类型分成两个；实例键随 `outerBindings` 而变，运行期特化键里是 GC 指针位；事实缓存键、常量比较用指针 | TY/T5，TY/T10，GE/B1，GE/B3，RW/A3，RW/A8 | TY/T5 T |
| C31 | 编译期开销没有上界或是平方级：折叠 `'ab'->repeat<1000000000>`、巨大 `zeros`；CSE `reaches`、DCE 重扫、`upstreamOf`、递归 SCC、`onCallCycle` 每个调用节点走全图、计划构建；宏每轮新建求值器且不缓存；`tee` 缓冲只增不减 | PA/B1，PA/C，GE/B2，ME/11，RW/C，MG/R7，MG/R11，ST/6，ST/15 | |
| C14 | 内部拒绝变成崩溃/空中止：特化不动点保护对优化抛无位置的 `runtime_error`（违反 5.11）；`.at()` 越界；推测性重载选择丢诊断后抛空 `BuildAbortException`；失败实例留下半成品图；`ASSERT` 在 release 为空却守着越界写（`graph.cpp:976`） | GE/A3，GE/A5，GE/A7，GE/B7，PA/D、PA/E | GE/A7 T |
| C30 | Tiles：算子列表手写在三处；只给测试用的核函数/区域遍在生产模块里；设备数组仍是 Tiles 私有包装；`Node.op` 按字符串分派 | OT/T2–OT/T5 | |
| C32 | 语言服务器：函数类型参数的 `=>` 被当括号，`map/filter/sort_by` 的签名提示坏；内建类型表缺 `bf16/f16`；取消打断不了 `analyze`、`catch(...)` 静默 | TO/5–TO/8 | |
| C33 | 元数据/兼容性握手只有一个整数（`.cmo`），库搜索路径全局放宽且不撤销（Windows DLL 植入面），核心加载器写死 `"python"`/`"pyplot"` | MG/R14 | |
| — | 其他单项：`tee` 按名字特判（见 C33）；`FunctionType::assignableFrom(Stream)` 方向可能反了（ST/7、TY 同） | ST/7 | T |

## 三、第三层：同一条规则实现多遍，或几份拷贝互相矛盾

| 组 | 同一条规则有几份 | 位置 |
|---|---|---|
| C1 | "能不能丢/能不能挪"：6 个谓词（`isValueOnly`、`mayBeRecomputed`、`nodeIsFreelySchedulable`、`mayHaveEffects`、`pureComputation`…） | PA/A1，RW/A1（同第一层） |
| C2 | 边的种类在至少 8 处手写枚举，同步边/顺序边时有时无（`findCycle`、`reaches`、`upstreamOf`、`gateInputsSurvive`…）；抽取转移等待边时同步边与控制边不对称。随"删控制边"一起统一成只读同步边与顺序边 | PA/A3，RW/A11 |
| C8 | 重载选择 3–4 套；`bindTypeVars` 与 `unifyTypeVars` 是同一递归两份（其中一个会修改已有绑定） | GE/B4，GE/B5，WH/A2 |
| C10 | 切片 6 份、第二套切片与取行、`:op/idx_take` 两个模块各注册一遍（后注册静默胜出）、`__builtin__idx__` 在一个 URI 里按运行期类型分派 | IX/1，IX/2，IX/9，AD/H2 |
| C4 | 转换三张表（矩阵、`castSlotFrom`、`convertTo`）；字面量 `as` 与变量 `as` 不同答 | TY/T4 |
| C22 | 结构体转换三处（`assignableFrom`/`castSafetyFrom`、`entersImplicitly`、`convertStructValue`）；`equals` 与驻留键的字段不一致 | TY/T7，TY/T3 |
| C14 | 图解释器 4 个：nvm、fvm（还有 gotovm/casevm 两份）、prl、宏求值器的 `executeGraph`（约 285 行，复刻调度器的弧对逻辑）；`attempt` 的边界 3 份 | BE/1，BE/2，MG/R11 |
| C16 | 模块搜索循环在 `context.cpp` 3 份；模块根构造在 6 个调用点各传各的选项（codegen 与调试器忽略 `-I`）；库搜索目录 3 份；条目模块构造 5 处；命令行两套解析器 | MG/R12，MG/R4，TO/9 |
| C13 | 故障转文本 3 份（宏、Python 绑定、standalone bridge） | MG/R10 |
| C11 | `buildStreamLoop` 是从 `visitLoopNode` 复制的 330 行，"构造元组"六遍 | ST/8 |
| C29 | `carriesOrdering`、`declaredCommutative` 和三个匹配器重复；`check` 用 `std::shuffle`（各家标准库不同） | RW/D |
| C30 | Tiles 支持的算子表 3 份 | OT/T2 |
| — | 语言服务器内建类型名第三份拷贝；`block` 装载逻辑在 `array.cpp` 里 5 遍；深度上限 CLI 与 `limits.cpp`、`standalone/core.h` 各读一遍；`when` 求值器与语言本体（求值器比语言宽松、混合数值类型） | TO/6，ME/9，TO/4，WH/B6，WH/C2 |
| C33 | 编译器按名字识别库函数：`"stream::tee"`；引擎里写死 `tensor:*` URI；加载器写死 `"python"`；`isPassDeclaration` 读 `when` 的文本；`Node.op` 字符串 | ST/5，AD/M7，MG/R14，GE/A6，OT/T5 |
| C15 | 环境变量开关：`CAMEL_TENSOR_ISA`、`CAMEL_ONNX_*`、`CAMEL_TIME_PHASES`、`CAMEL_COLL_*`、`CAMEL_TILES_TEST_INVALIDATION`（产品代码里）、约 17 个 `CAMEL_GC_*`（非法值静默变成"不限制/开"）、"legacy"的 `CAMEL_MODULE_PATH`、`CAMEL_HOME` 不校验且遍和模块的优先级不一致 | TO/4，MG/R6，MG/R13，PY/P8 |

## 四、第四层：结构（大函数、死代码、历史叙述）

- **超长函数**（拆成具名步骤）：`pullback.cpp` 4738 行（`Backward::run` 342 行）、`gct/builder.cpp` 6404 行（`visitReservedExpr` 431、`visitFuncType` 317、`visitModule` 314）、`exporter.cpp` 5021 行（`evalCall` ≈450、`evalIndirectCall` ≈416、`translateLoop` ≈345）、`compiler.cpp`（Tiles，`operation` ≈420）、`runCommand` 670、`rewrite` 的 `module.cpp` 4179、`instantiateForCall` 413、`instantiateGeneric` 347、`evaluateWhenCondition` 410、`evaluateCallAtBuildTime` 320、`applyRuntimeOptimizeRewrite` ≈360、`footprintOf`、测试运行器 2877 行。（AD 大函数一节，OT/T5，TO/3，RW/E，GE/C，WH/C1，PA/F，RS 小处，TO/11）
- **死代码与过时**：`loop_driver.h` 的 `driveLoopRounds`、`ExitType::Raise/Throw`、`known_fail`、`appendArrayVjp` 一类无调用者（BE/8，TY 低项，TO/11）；`elemTypeOf` 重复分支、`unstatedBindingIn` 的 Stream 死分支（OT/O9，TY 低项）；ONNX 里 opset≠17 的分支（OT/O7）；`castSlotFrom` 里不可达分支（TY/T4）；`IX/11`、`ST/18`。
- **自测代码在生产源文件里**：`gen.cpp` 的 `debugRun…SelfTest`、`foreign_handle.cpp`、`array_type_selftest.cpp`（ME/13，MG 低项）。
- **历史叙述型注释**（改成只写规则）：AD 与 TY 报告末尾各有清单（`derivative.cpp`、`text.cpp:564`、`intern.h` 头、`primary.h:322`…）；OT 低项；`taskflow.cpp:318`（引用测试名和计数）、`install_layout.h:156`、`gct/builder.cpp:4396`（引用任务名）；ST/17、IX/12、GE/D、WH/D、ME/14、FE/C 与 FE"附"的 `throw`/`?->`/`module` 清单。
- **悬而未决的标记**：代码里 23 处 `DESIGN-QUESTION`（BE 返工第 5 条要求汇入一个问题页），`tools/debugger/` 另有 5 处。
- **文档与实现各说各话**：入口模块名（原则 10.2 对 `"main"`，C16）、`python_rules.cml` 仍用 `throw`（PY 低项）、`module` 声明未删完（questions 的"module 声明没删完：返工"）。

---

## 五、改动组（协调者排这个）

建议的先后：C1、C3、C24、C16 先做（静默错误或进程级状态，且别的组依赖它们）；C5、C6、C26 随后（需要 ORT/有限差分用例，可以并行）；C8/C9/C10/C11/C12/C29 各自成链；第三、四层的整理放在对应第一层的修复之后做（同一处代码只动一次）。

| 组 | 改动 | 包含的条目 | 依赖/备注 |
|---|---|---|---|
| **C1** | 在核心里合成**一个**"纯且不会出故障"谓词，读算子的标准性质（效应、可能故障；`Cast` 会失败，`as_shape_fault.cml`），DCE、CSE、内联、改写、调度全部调用，删掉另外 5 个；`consistency/faults` 用例加带遍流水线的跑法 | RW/A1，PA/A1，PA/A2，PA/G | 与 C2 同时做；C29 的 `pureComputation` 也在这里消失 |
| C2 | 随"删控制边"统一成只读同步边与顺序边，边的种类枚举收成一处 | PA/A3，RW/A11 | 在 C1 之后 |
| **C3** | "能否原地写"只有一条规则：看实际引用计数（内联数组、槽数组、元组/实参/捕获产生的第二持有者都经过同一处）；逃逸分析数到上限当作逃逸；prl 的原地计划按偏序重推 | ME/1（已修）、ME/2、ME/3、BE/7 | 先写 ME/2、ME/3、BE/7 的用例 |
| **C4** | 数值转换只留一张表，按原则 5.1 裁定 `as` 允许什么；字面量与变量同答；Python 侧 `py::cast` 改成拒绝 | TY/T4，PY/P7，PY/P5，IX/6，IX/7 | 先要负责人裁定"`as` 的数值互转" |
| **C5** | ONNX 的原则：导出的模型算出的和运行时一样，做不到就拒绝或插检查节点；共用一个下标守卫（抽自切片的 Gather 零向量守卫）；沿轴归约先扩到 f64；`max/min/relu` 复用 `argmax` 的 NaN/±0 组合；每条配 ORT 对照用例；能力表只放行已探测算子；删 opset≠17 分支 | OT/O1–O10，IX/3，IX/4，AD/M8 | 与 C6 共用"按元素类型参数化" |
| **C6** | 梯度/推断内核按元素类型参数化（f32/f64 两臂，其余拒绝）；推断函数跟随输入 dtype；`sum_to` 按 7.3 在 f64 累加（先裁定） | AD/H3，AD/M6，AD/L1，IX/8，OT/O4，OT/O5 | 补 f64 有限差分 |
| C7 | 梯度语义：并列最大值的规则（倾向只给第一个，与 `argmax` 一致，写进原则）；规则必须声明每个活跃输入（显式"零"）；类型检查含形状；标量 `math` 规则补齐；二阶覆盖范围写明 | AD/H4，AD/M1–M5 | 补并列值、标量数学用例 |
| **C8** | 收成**一个**重载选择（`when`、`with`、字面量、歧义同一套）；`one()` 只用于确实只有一个的场合；`bindTypeVars`/`unifyTypeVars` 合并 | GE/A1，GE/A2，GE/A4，GE/B4，GE/B5，WH/A2 | 先于 C9 |
| **C9** | 编译期求值不猜：未绑定类型变量不求值；按声明处作用域求值；只接已知的诊断；失败种类不丢；`with` 值进谓词实参 | WH/A1，WH/A3，WH/A4，WH/B1–B8，GE/A3，GE/A6 | 在 C8 之后 |
| **C10** | 调用处用声明的默认值补齐切片六个词，六处推导删除；删第二套切片与取行；`:op/idx_take` 的导数规则只注册一处；切片轴为负是故障；元素类型规则不被"常量"绕开 | IX/1，IX/2，IX/5–IX/7，IX/9，AD/H2 | 与 C24 的"导数只注册一次"共用 |
| **C11** | 线性检查改成建图后的一遍：每个流类型值的数据出度≤1，`tee` 各结果各算一个，互斥臂合计一次；删按名字追踪的那套；`tee` 不按名字特判；`buildStreamLoop` 与 `visitLoopNode` 共用；修 `assignableFrom(Stream)` 方向 | ST/1–ST/5，ST/7，ST/8，ST/10 | |
| **C12** | 资源度量从底开始不动点（所有函数体先记空集，按被调者当前集合迭代取并）；量不出来就报，或按最保守读法；第 256 个参数不能丢；工作表增量 | RS/1–RS/3 | |
| **C13** | 异常只在能处理的层接：宏求值只接 `MacroExecutionError`/`Diagnostic`/`RuntimeFault`；`isIsolatableFault` 默认可隔离，只列出不可隔离的；`attempt` 边界收成一个共用实现；故障转文本走一处 | MG/R9，MG/R10，BE/2，BE/5，PY/P7，WH/A4，RW/A7，ST/3 | 先写 BE/4 的用例钉住现状（语义待负责人定） |
| **C14** | 一个解释器核心：fvm 留一份，派发方式只在宏层选择；宏求值器复用节点 VM；prl 的循环进并行计划，图不一致报缺陷；内部拒绝一律带位置的诊断，不抛 `runtime_error`；`ASSERT` 守护的越界写改真实检查 | BE/1，BE/2，BE/3，MG/R11，GE/A5，GE/A7，PA/E | 大工程，分步 |
| **C15** | 删环境变量开关（调试开关与生产参数分离成命令行选项）；`CAMEL_HOME` 校验；删 `CAMEL_MODULE_PATH`；非法值报错 | TO/4，MG/R6，MG/R13，PY/P8 | |
| **C16** | 一个 `ModuleResolver`：自己目录 → 模块根 → 标准库；只认 `.cml`/`.cmo`；`.cmo` 命中后加载失败终止导入并报错；入口模块名取文件名（先裁定原则与代码以谁为准）；目录缓存随搜索路径变化失效；删向上前缀回退或写进原则；所有工具入口走同一处 | MG/R2，MG/R3，MG/R4，MG/R12，MG/R15，TO/9 | 先写 MG/R2、R3 的用例 |
| **C18** | 限制和回退必须说出来：达到上限给带位置的警告；`#gpu` 范围的不支持要报错并点名；prl 回退时可见；泛型规则按匹配处实例化，不设静默上限 | BE/3，PA/D，OT/T1，RW/A5 | |
| **C19** | 缓存一律用规范 id / 声明身份作键；`pure` 缓存不缓存未定义完的结构体；事实缓存键含声明身份、类型实参、`with` 规范形式 | TY/T5，TY/T10，GE/B1，GE/B3，RW/A3，RW/A8，AD/H1（缓存部分） | |
| **C20** | 打印与解析共用"需要括号"的判断；打印后重新解析得到同一类型的往返性质测试覆盖所有类型构造；资源子句中无法命名的项另行处理 | TY/T1，FE/C1 | |
| **C21** | 前端按规范：一元优先级；未闭合字符串在行尾/文件尾报；前导零统一；接受就能用，否则明确语法错误（`t[a:b, c]`、`fact` 类型参数、`yield`、`{ ...`） | FE/A1，FE/A2，FE/A4，FE/A5，FE/B，IX/5 | |
| **C22** | 一个类型一个身份：空指针一律假；`equals`、`assignableFrom`、驻留键读同一组字段；补聚合内的 `unify`；结构体"按名字对应"转换只写一处；`Void` 目标限定在函数返回位置 | TY/T2，TY/T3，TY/T6–TY/T8 | |
| **C24** | 进程级状态按所有者登记/注销（根集合、清扫器、导数规则、切空间）；收集中途异常视为致命；协作式 STW 放弃后升级（诊断 + 阻塞算子进入前 `leave()`）；视图/延迟释放的生命周期 | MG/R1，MG/R5，MG/R8，AD/H1，ME/5–ME/8，BE/6 | 先写 MG/R1、R5 的用例 |
| **C25** | 跨边界数组只读（DLPack 1.0 只读标志 + `writeable=False`）；导入成功后才改名；异常在边界全部转换为 Python 异常 | ME/4，PY/P1–P3，PY/P13 | |
| C27 | Python 互操作按 §10.7 补齐（泛型导出、释放 GIL）、多 Context、路径与重载 | PY/P4，PY/P5，PY/P8，PY/P10 | 依赖 C24 |
| **C28** | 稳定 ABI（abi3）覆盖三个平台；冒烟测试 `--no-index` 并真调用 | PY/P6，PY/P11，PY/P12 | 按交接说明 5.37 |
| C29 | 事实与规则：先确认数据边能否成环，决定实现格迭代或删 `meet/widen/narrow`；分开编号；`check` 用确定性洗牌 | RW/A2–A6，RW/A9，RW/B | 先测 |
| C30 | Tiles 支持的算子表合成一份；测试专用的核函数与区域遍移出生产模块；`Node.op` 改枚举；设备数组随数据块拥有设备内存一起改 | OT/T2–OT/T5 | 与 C18 的 `#gpu` 拒绝一起 |
| C31 | 折叠设大小/时间上限；平方级算法换成增量/缓存；宏跨轮缓存；`tee` 缓冲收缩 | PA/B1，PA/C，GE/B2，ME/11，RW/C，MG/R7，ST/6 | |
| C32 | 工具：不起作用的选项实现或删除；退出码统一；`runCommand` 拆开；LS 的签名与类型表 | TO/1–TO/3，TO/5–TO/8 | |
| C33 | 库函数不靠名字识别：由声明和算子性质给出行为；引擎里的 URI 改成算子 trait | ST/5，AD/M7，MG/R14，GE/A6，OT/T5 | |

---

## 六、需要先写用例确认的条目（汇总）

报告自己标"待验证/需先写用例"或我读代码后认为失败场景尚未跑过的：

- ME/2、ME/3、BE/7（C3）——**ME/1 已修复，不再需要**。
- PA/A1、PA/A2 已有场景，补 `consistency/faults` 带遍流水线的跑法（C1）；PA/A3 中"同步函数的常量 GATE 被内联后失去用户"标待验证。
- RW/A2 前提"数据边能否成环"待验证（C29）。
- AD/H1（同一进程第二个 Context）、AD/H3（f64 的 `gelu/layer_norm` 有限差分）、AD/H4（并列值）（C24、C6、C7）。
- TY/T1（往返）、TY/T4（`ASSERT` 在 release 下的行为，以及 `as` 在字面量与变量上的差别）、TY/T5（声明到一半的命名结构体）（C20、C4、C19）。
- OT/O2（CUDA EP 对 `Gather` 越界的行为）、OT/O6（`layer_norm` 二维 gamma）、OT/T1（`#gpu` 运行期重试后是否报错）（C5、C18）。
- MG/R1、MG/R2、MG/R3、MG/R5（两个 Context 的 major GC、`.cmo` ABI 不符且有同名内建、目录里有 `foo.md`、长时间不 poll 的算子下堆有界）（C24、C16）。
- PY/P10（GIL 与收集的锁序）、PY/P8（API 不跑遍流水线时 `grad` 与 `#gpu` 的行为）（C27）。
- BE/9（JIT 的 `LDIV` 标志，复制粘贴来的）、BE/4（先钉住现状，语义待负责人定）。
- GE/A7（`.at()` 越界的 `constas` 路径）、ST/7（`assignableFrom(Stream)` 方向）、ST 报告里标"待验证"的 13/14 条。
- WH 报告里标"待验证"的几条（`literalOf` 混合数值类型的比较等）。

---

## 七、反复被违反的规则（建议写进 `agents/` 的指引）

每条给三个例子，位置见各报告。

1. **拒绝必须说出来，不能静默回退。** 写了就要检查，查不了就报，不能跳过或换条路走。
   - prl 遇循环整图退成串行，只在诊断日志里一行，还吞掉真正的图不一致（BE/3）。
   - `.cmo` 加载失败静默换下一个候选名或同名内建模块（MG/R2）；`#gpu` 范围里没解析的类型返回空，节点静默留主机（OT/T1）。
   - 资源子句在度量放弃时被跳过（RS/2）；内联/会话/轮数上限只写日志（PA/D）。

2. **一条规则一份实现。** 同一件事出现第二份，就必须删掉一份，而不是保持同步。
   - "能不能丢"六个谓词（PA/A1）；切片的默认值在六处各推一遍（IX/1）；重载选择三四套（GE/B4）。
   - 数值转换三张表（TY/T4）；故障转文本三份（MG/R10）；fvm 两份解释器（BE/1）。

3. **"纯"必须是"纯且不会出故障"。** 故障是算子的标准性质，不能只问效应。
   - DCE 丢掉 `let r = 5 / z` 的故障（PA/A1）；改写 `(a/b)*0 => 0` 丢除零（RW/A1）。
   - ONNX 的除法和数值转换丢了运行时的故障（OT/O1）；`isIsolatableFault` 默认"不隔离"（BE/5）。

4. **导出的、打印的、转换的形式必须与运行时是同一个意思。** 做不到就拒绝。
   - ONNX 的负下标、归约精度、NaN/±0（OT/O2、O3）；打印出的 `int?[]` 写不出、函数数组含义变了（TY/T1）。
   - `5 as float` 与 `n as float` 不同答（TY/T4）；Python 边界上 `5` 变 `true`（PY/P7）。

5. **不要用环境变量切换行为；开关写成命令行选项，非法值报错。**
   - `CAMEL_TENSOR_ISA`、`CAMEL_ONNX_*`、`CAMEL_TILES_TEST_INVALIDATION` 在产品代码里（TO/4）。
   - `CAMEL_GC_*` 约 17 个，非法值静默变成"不限制/开"（MG/R6）；"legacy" 的 `CAMEL_MODULE_PATH`、不校验的 `CAMEL_HOME`（MG/R13）。

6. **注释写规则，不写历史、任务名、日期、测试计数。**
   - `taskflow.cpp:318`（"skipped 3,525 of the 3,532 polls …"）；`gct/builder.cpp:4396`（任务名）；`primary.h:322`（"it used to be cut to 32 bits"）。
   - `intern.h` 头（"Before it, every construction…"）；`text.cpp:564`（"what the tensor type's equality did before"）；`derivative.cpp` 里 `f32[2,3]` 曾经返回 `f32[]` 的叙述（AD 历史注释一节）。

7. **`catch` 只接能处理的那一种，内部错误照内部错误报。**
   - 宏求值把 `logic_error`、`MemoryExhausted` 报成用户的宏失败（MG/R9）；`pyToCamel` 的 `catch (...)`（PY/P7）。
   - `when` 的 `catch (...)` 吞掉 `BuildAbortException`（WH/A4）；`leaveScope` 在 `catch(...)` 里跑检查盖掉真错误（ST/3）。

8. **缓存的键是身份，不是文本、指针或地址。** 类型用规范 id，声明用声明身份。
   - `pure` 缓存以指针为键，可能缓存声明到一半的命名结构体（TY/T5）；运行期特化键里是 GC 指针位（GE/B3）。
   - 事实缓存键是"名字 + `with` 文本"（RW/A3）；常量比较用指针相等而别处用规范 id（RW/A8）。

9. **进程级状态按所有者登记、按所有者注销，不是"后来者覆盖"。**
   - 分配器的根集合与图清扫器被每个 `Context` 覆盖、销毁时清空（MG/R1）；每个 `Context` 重新注册导数并清掉张量切空间（AD/H1）。
   - Python 绑定只有一个共享 Context，后来的搜索路径被忽略（PY/P4）；`.cmo` 加载全局放宽 DLL 搜索路径且不撤销（MG/R14）。

10. **通用层不认具体名字。** 名字是语义的一部分时，把它写成声明或算子 trait。
    - 编译器按名字特判 `"stream::tee"`（ST/5）；加载器写死 `"python"`、`"pyplot"`（MG/R14）。
    - 引擎里写死 `tensor:where`、`tensor:shape` 等 URI（AD/M7）；`isPassDeclaration` 读 `when` 的文本（GE/A6）。

11. **接受了就要能用，否则是清楚的语法/类型错误。** 不留"解析得过、后面才说不支持"和不起作用的选项。
    - `-n/--no-cache`、`-S/--scheduler`、`inspect -p`、`check -c/-e/-o` 没有读取方（TO/1）。
    - `t[:, 0]`、`t[a:b, c]`、带类型参数的 `fact`、`yield`、`{ ...` 字面量（FE/B，IX/5）。

12. **类型随元素类型参数化，不写死 f32。**
    - `gelu_grad/erf_grad/layer_norm_grad` 按 f32 读 f64（AD/H3）；`permute_inverse`、`layer_norm_grad` 的推断写死 f32（IX/8）。
    - ONNX 的 `softmax/layer_norm/conv` 把 f64 转 f32（OT/O4）；`full`/`zeros` 动态形状给错元素类型（OT/O5）。

13. **每条跨后端/跨路径的规则配一条一致性用例，覆盖所有后端。** 否则"一份测一份用"（fvm）或"测 f32 漏 f64"（梯度）。
    - fvm 两份解释器每个平台只编一份（BE/1）；`tensor_grad_check` 只有 f32（AD 测试缺口）。
    - ONNX 对照 ORT 的用例没有 softmax、layer_norm、f64、沿轴归约（OT 测试缺口）。
