# 重构③第 2、5 步：遍层按遍名分支——站点清单与归并

来源：`internal/plans/2026-11-28-daily-review.cn.md`（fsh-dev@1188fb1b0d）第 1 条。
范围：只做**读数与归并方案**，不改代码；改动归重构③"遍即函数"第 2 步（"登记只有算子表一处"）与第 5 步（"遍即函数"）。

## 一句话

这一个和 α 是**同一个病的两面**：α 治的是"一个遍有两个名字，声明没说清谁是谁"（源码名 `std::cxx::cse` ↔ 算子 URI `std:cxx::cse`，声明 `#op` 把两者连起来）；
这一条治的是"遍层把**名字文本**当成遍的**性质**"（是不是调度遍、能不能被调试器停住、要不要代码生成器、默认跑哪个后端）。
两面同源：只要遍的名字还在代码里被当成事实来分支，名字就还没有"只有声明一处"；α 之后声明能回答名字与 URI，这一条要声明接着回答性质。

## 一、按名字分支的全部站点（读数，逐处贴原文）

计数口径：`git grep -n '\bstd::<名>\b'` 去掉注释行（`//`、`*` 开头）后计"非注释命中"，其中"分支/表项"=用名字做判断或写进常量表。
`std::opt` 用 `\b` 收尾，`std::optional` 不计入。

| 名字 | 非注释命中 | 其中分支/表项 | 站点点名 |
|---|---|---|---|
| `std::jit` | 22 | 4 | `pass.cpp:2550`（缺代码生成器告警）、`debugger/src/run.cpp:124`、`fastvm.cpp:1648`（注册，不是分支）、`fastvm.cpp:589`（按策略选报告名） |
| `std::profile` | 5 | 1 | `pass.cpp:2629`（`pipelineNamesProfile`） |
| `std::opt` | 44 | 7 | `pass.cpp:91`（标签读者名单）、`pass.cpp:678`（`settle_runs` 下限）、`pass.cpp:2366`（流水线重写表）、`opt.cpp:45`（注册）、`engine.cpp:1494`/`:1570`（报告名）、`generic.cpp:2135`/`:2139`（日志） |
| `std::run` | 8 | 5 | 见下节五处默认 + 内核自身两处（`:101` 常量、`:2443` 注册） |

### 内核的四处"按名字分支"（原文）

```cpp
// src/kernel/execute/pass.cpp:2550  —— 缺代码生成器时告警
if (specName(spec) != "std::jit") { continue; }

// src/kernel/execute/pass.cpp:2629  —— 名字里有没有 profile
bool pipelineNamesProfile(const std::vector<std::string> &passes) {
    for (const auto &spec : passes) {
        if (specName(spec) == "std::profile") { return true; }

// src/kernel/execute/pass.cpp:678  —— 形参下限按名字判
if (name == "std::opt" && args.given("settle_runs") && args.getInt("settle_runs") < 1) {

// src/kernel/execute/pass.cpp:2471  —— 调度遍清单写成文案
"`passes.run` names '{}', which is not a scheduling pass; `{}` runs the "
"program, so it must name one (std::fvm, std::nvm, std::jit, std::prl)",
```
`:2471` 要分清：**判定不是名字**——`requireRunPassTarget`（`:2465`）判的是 `dynamic_cast<GraphSchedulePass *>`，字面量清单只在**文案**里。
`pass.cpp:91` 是 `tag::declareReader` 的名单（`std::opt`、`std::cxx` 等）——它问的是"哪些遍读标签事实"，也是遍的性质。
`pass.cpp:2366` 是 `{"std::defunctionalize report=false", "std::opt"}` 这类重写表，属"遍的选择"，不是性质。

### 调试器两处（原文）

```cpp
// src/tools/debugger/src/run.cpp:124-140
if (pass == "std::jit") { reason = "compiled code cannot be stopped"; }
else if (pass == "std::prl") { reason = "…the order of evaluation under the debugger is the sequential one of std::fvm"; }
… notice += "[camel-db] " + pass + " runs as std::fvm under the debugger: " + reason + ".";
out.push_back("std::fvm");
```

## 二、`std::run` 的默认写了五处（逐处原文）

```cpp
// ① 命令行：src/tools/camel-cli/config.cpp:360
vector<string> fallbackPasses   = {"std::run"};

// ② .opencmlrc：src/tools/camel-cli/opencmlrc.cpp:210
constexpr const char *DEFAULT_FALLBACK_PASS = "std::run";

// ③ 调试器：src/tools/debugger/src/run.cpp:276
static const std::vector<std::string> defaultFallback{"std::run"};

// ④ 调试器服务端：src/tools/debugger/server.cpp:1181
if (passes.empty()) { stages.push_back({{"id", "std::run"}, {"label", "fallback"}}); }

// ⑤ 流水线模型脚本：scripts/camel-pass-pipelines.mjs:42
'std::run': 'std::fvm',
```

**唯一该留的一处**：内核里那对注册——`src/kernel/execute/pass.cpp:2443` 的

```cpp
const camel::execute::PassKernel
    runPassKernel("std::run", [](const context_ptr_t &ctx, const PassArgs &args) {
        return makeRunPass(ctx, args);
    });
```

它和 `std.cml` 里 `std::run` 的声明是一对（`test/tools/check-pass-declarations.mjs` 逐对核），别的文件都只是问"兜底是哪个"。**其余五处是什么**：

- ①是命令行在没写遍时的默认列表——消费者，应问"哪个声明是兜底"；
- ②是 rc 的默认列表——同上，且它与①就是同一件事的两份；
- ③是调试器自己的默认兜底——同上；
- ④不是遍，是调试器阶段列表里的一个 id/label——应由②/内核的兜底名得到，不该另写；
- ⑤是脚本模型把"兜底遍"直接折成默认后端 `std::fvm`——两份事实（兜底名、默认目标）都写死了。

另有第六处默认目标的副本：`pass.cpp:102` 的 `kRunPassDefaultTarget = "std::fvm"`（`runPassTarget()` 在 `passes.run` 为空时用它，`:2406`）。它和 `std.cml:445` 的说明是同一事实的两处，应与声明合并。

## 三、该并成什么（归重构③第 2、5 步）

1. **名字只在声明里写一次**（第 2 步已由 α 立起形状）：`std::run` 的名字在 `std.cml` 的声明 + 它的 kernel 注册，其余处读算子表/声明，不写字面量。
2. **性质写在声明上，遍层读记录**：
   - 是不是调度遍：今天靠 `GraphSchedulePass` 类型（`pass.cpp:2465`），保留；只删 `:2471` 文案里的名字清单，改成"`passes.run` 必须命名一个调度遍"。
   - 能不能被调试器停住：声明上加性质（如 `#debugger(stoppable=false)`），`debuggablePasses` 读它并把替代遍也从声明取，不再按 `std::jit`/`std::prl` 两个名字分支。
   - 要不要代码生成器：声明上加性质（如 `#native_codegen(required)`），`jitWithoutCodeGeneratorWarning`（`:2550`）读记录，不再比名字。
   - `settle_runs` 的下限：写进声明的形参类型（`settle_runs: int` 的下限），由形参检查统一报（`ParamProblem` 已存在，`pass.cpp:678` 那一段整体消失）。
   - 哪个遍读标签事实：`pass.cpp:91` 的名单改读声明上的 `#reads` 事实（Camel 遍已有这条通路）。
3. **兜底遍与它的默认目标只在声明一处**：`std.cml` 的 `std::run` 声明带"兜底"性质与默认目标（`std::fvm`），①–⑤ 与 `pass.cpp:102` 都读它；`passes.run` 覆盖时仍按现在的顺序（rc → 内核默认）。
4. **调试器阶段 id**：`server.cpp:1181` 从兜底名的来源取，不另写。
5. **脚本**：`camel-pass-pipelines.mjs` 的 `ALIASES['std::run']` 改为读同一处事实；它的注释已经写明"目标来自运行配置"。

## 四、与我这一笔（α 第 1 笔）的对齐

- α 已落地：`std.cml` 的 `namespace cxx` 七条声明带 `#op('std:cxx::<name>')`，对应七条 `PassKernel` 注册串改成 URI；`PassDeclaration::operatorUri` 让"写出的源码名"经**声明**落到 URI；`findDescriptor` 先按写出的整串查（行为不变），失败才用声明写的 URI 再查一次。
- 行号对齐（review 基于 develop `f4484f111..36f7b2adf5`，本页基于本车道 `5e3da5d4f6` + α）：review 说的 `specName(spec) != "std::jit"`＝本树 `pass.cpp:2550`；`== "std::profile"`＝`:2629`；`name == "std::opt" && settle_runs < 1`＝`:678`；"目标清单写成字面量、默认 `std::fvm` 写死"＝本树 `:2471`（只是文案）+ `:102`；"兜底遍写了四遍"＝本树 `config.cpp:360`、`opencmlrc.cpp:210`、`debugger/src/run.cpp:276`、`server.cpp:1181`，加脚本 `camel-pass-pipelines.mjs:42` 共五处——与 review 的五处一致。
- 同一病的两面：α 让"名字"和"URI"在**声明**处接上；本页让"名字"不再被遍层当成**性质**。两件都做完，遍的名字才真正只有声明一处。

## 五、边界与待裁

- 本页最初只做读数；其中能不动语言表面的那一块**已做**（见下节）。
- review 第 1 条的"顺带"（把 `check-pass-declarations.mjs` 里靠 `passAliases` 变量名把关换成行为用例）与 lead 2026-11-28 的裁定（保留 `:411` 该守卫）方向相反，请决策者裁一句：守卫留着、另加行为用例，还是替换。

## 六、已做与未做（按上面的清单）

**已做（`4d7e0ac41b`，不动语言表面）**

- 兜底遍的名字只剩内核一处：`runPassName()` / `runPassDefaultTarget()`（`pass.cpp:2409`、`pass/base.h:139`），`config.cpp:361`、`opencmlrc.cpp:211`、`debugger/src/run.cpp:276`、`server.cpp:1182` 改读它。脚本 `camel-pass-pipelines.mjs:42` 读不到内核，**仍未做**，留在清单里。
- `:2471` 的调度遍文案删掉后端名单：判定本来就是 `dynamic_cast<GraphSchedulePass *>`（`:2465`），句子不再列举后端。
- 读数：`smoke` 10/10；`feat/compile` 13 全丙、`feat/modules` 20 全丙、`feat/opt` 7 全丙（新增红 0）。

**未做（都要动语言表面，需决策者裁拼写）**

- "要不要代码生成器"（`pass.cpp:2550`）、"能不能被调试器停住"（`debugger/src/run.cpp:124/126`）、"名字里有没有 profile"（`pass.cpp:2629`）——今天都没有可读的声明事实。要写，就得在遍声明上加性质，例如 `#native_codegen(required)`、`#debugger(stoppable=false, replace="std::fvm")`、`#profiles`。这是语言表面的新语法，不按我的口味定。
- `settle_runs: int` 的下限（`pass.cpp:678`）：`PassParam` 今天只有类型（`params.h:59` 四个字段，没有范围），要把下限写进声明就得给 `with` 的参数语法加范围（例如 `settle_runs: int >= 1`），同样是语言表面。
- 调试器把 `std::jit`/`std::prl` 替换成 `std::fvm` 这件事，除了上面那个性质，还牵着"替代成哪个"的取值，也要在声明里写。
- `pass.cpp:91` 的标签读者名单：Camel 遍有 `#reads` 这条路，C++ 遍还没有对应的声明事实，同上。