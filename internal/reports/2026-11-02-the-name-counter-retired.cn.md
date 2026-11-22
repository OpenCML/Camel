# 2026-11-02：名字覆盖计数器退役（P2-19）

## 它原来是这么数的

`scripts/diagnostics-catalog.mjs --name-coverage`：

- **分母** = 注册表里 `type === 'SemanticDiag'` 的条目中，**有某个 C++ 文件"提到" `SemanticDiag::X`**
  的那些（用正则扫 `src/`、`include/`、`modules/`，并排除 `diagnostics/messages/`）。今天 **239**。
- **分子** = 上述条目里，**没有任何断言**的那些。今天 **5**（`WithPackNeedsNativeDeclaration`、
  `CmlUnsupportedConstruct`、`CmlBigConstant`、`NodeOfNotAParameter`、`TagHasNoReader`）。
- "有断言"的认法：把 `test/plans/**/*.plan.toml` 的注释和 `desc` 删掉后，正则找四类文本
  —— `expect_diagnostic_name`、`expect_diagnostic_code`、`verify_script` 里的 `.name ===` /
  `.code ===`，**以及任何地方出现的 `name=SemanticDiag::X`、`code=0x…`**。

## 为什么它能被压

1. **分母可调**：它靠"源文件里出现 `SemanticDiag::X`"来判定这个码"有人抛"。换个写法（走别名、
   走查表、走 helper 参数）就不会被扫到，码从分母里消失，比例立刻好看——**识别口径一变，数字就变**。
2. **分子可调**：最后一类认法是**自由文本**。一个 needle 里带上 `name=SemanticDiag::X` 就算"断言过"，
   哪怕那条 `needle` 与这个码毫无关系；`verify_script` 里写在注释里也算。
3. **它是棘轮**：`NAME_COVERAGE_RECORDED` 在"加用例的那个提交"里下调，于是"集合没有变大"可以与
   "集合被换个读法"同时成立。**测量工具自己也会造假**，这正是 P2-19 点名的那条。

## 退役后的门禁：`catalog.pinned_coverage`

新脚本模式 `--pinned-coverage` / `--pinned-coverage-check`（`compiled.diag_catalog`）：

- **枚举来源只有一处**：`diagnostics/registry.tsv`，经 `catalogEntries()`，与 `--check`、
  `docs/diagnostics.md` 的生成走同一条路；**不扫源文件**，所以码不可能因为写法而掉出分母。
- **只认按码断言**：`expect_diagnostic_name`、`expect_diagnostic_code`、`verify_script` 里的
  `.name ===` / `.code ===`。**自由文本一律不读**。
- **具名例外**：目前一条 —— `InternalDiag::UnknownInternalError`，理由随清单一起打印（"编译器发现自己
  没有对应码的状态"，没有程序能触发它）。**不静默跳过**。
- **记录值是地板**：`PINNED_COVERAGE_RECORDED = 27`（今天的实数），只在"加用例的同一个提交"里下调，
  不许上调；没有为抬数字加任何用例。

## 两个数并列，分母不同，不要混着比

| 计数器 | 数 | 分母是什么 | 认法 |
|---|---|---|---|
| 旧的 `--name-coverage` | **5 of 239** | 语义段里"有源文件提到"的码 | 自由文本也算 |
| 新的 `--pinned-coverage` | **279 of 307** | 注册表全部码 | 只认按码断言 |

新数**比旧数高**，不是因为覆盖突然变好，而是因为**分母换成了整张注册表**（lexical/syntax/internal 段
里大量码本来就有 `expect_diagnostic_code`），且旧的分子只数"语义段里没人按名断言的"。**这两个数字不能
相减、不能相除**。

## 之后的用法

- **`--pinned-coverage-check` 是门禁**（`catalog.pinned_coverage`）。
- **`--name-coverage` 只是测量**（`catalog.name_coverage`，`expect_exit = 0`、只要求它打印），页里与
  用例 desc 都写明"不再是目标"。它保留的理由：那个数仍值得看，而且它是"同一件事的两种读法"的活标本。
- 遇到确实不可能被触发的码：加进 `PINNED_COVERAGE_EXCEPTIONS` 并写清理由，**不许悄悄跳过**。

## 顺带记一笔：`CmlBigConstant`

按 2026-11-02 的裁定（原则 2.5 澄清：大常量照原样打印成字面量），`CmlBigConstant` 要被删。它今天还在
注册表里（307 个码之内），也不在"按码断言"的 279 里；删码落地后按 `307 → 306` 记账，本条记录的
`27` 与 `279` 都要跟着复核——**删码与改计数必须同一个提交**，否则分母又被人为动过。