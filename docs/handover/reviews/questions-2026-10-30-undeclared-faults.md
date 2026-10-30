# 没声明 `faults` 的算子：第三态，还是一张没写完的表

裁定：**没声明按“可能出故障”读，第三态有意义、读数保守；但表要补完——已经核对过的族现在落地，
其余按 reach 补。**（`fsh-dev` 上 `review: an undeclared fault list reads as may-fault and the table
is to be completed; land the audited families now and the rest by reach`。）落地结果：
`docs/handover/reports/faults-table-audit.md` 补了 17 行，并把“按 reach 补的规矩”写成那一节；
`test/plans/feat/modules/operator_faults.plan.toml` 里 `declared_traits` 用 `tensor::sum` 钉住
“没声明 ⇒ 读 1”这一条。

算子的语义记录里没有 `faults` 这一项时，今天的读法是“可能出错”：`operator_table.cpp#OperatorTable::traitsOf`
写的是 `mayFault = !facts.faults || !facts.faults->empty()`，`operator_table.h` 的注释也这么说
（“No fault list: undeclared, may fault”）。问题不是这条读法对不对，而是：**“没声明”是一个要一直
留着、有意义的状态，还是只是一张还没填完的表？** 下面的问题与选项保留原样，作为这条路的记录。

## 现象到符号

这一轮把**声明了空表**的算子逐个核对完了（`docs/handover/reports/faults-table-audit.md`，77 行）：
77 行的实现里都没有出错路径，所以一条声明也没改。核对时碰到一批“实现也不出错、但一个字的声明都没有”
的算子，它们不属于那一笔的范围（补上去会改 `may_fault` 的读数），留在这里：

- **同一个内核，一半声明了一半没声明**：`:op/pow_bf`、`:op/pow_f6` 声明空表，而 `:op/pow_i`、
  `:op/pow_l`、`:op/pow_f`、`:op/pow_d` 连语义记录都没有（`builtin_semantics.cpp#declareBuiltinOperatorSemantics`
  的文件头也这么写）。两者走的是同一个 `std::pow`（`ops.cpp#__builtin__pow__`、`half_ops.cpp#__builtin__pow_bfbf__`），
  都是定义域外得 NaN、溢出得 inf，不报错。
- **复数的乘法与取负**：`:op/mul_c`、`:op/mul_z`、`:op/neg_c`、`:op/neg_z` 的实现（`ops.cpp#complexBinary`、
  `ops.cpp#__builtin__neg_c__`）同样没有出错路径，但它们不在 `kNeverFaults` 里、记录也不带表：现在算“可能出错”。
- **`len`**：`:struct/len_arr`（`struct.cpp#__len_arr__`，一个 `arr->size()`）、`:struct/len_str`、
  `:coll/len`，一个字的声明都没有，实现也没有抛点。裁定点名过它。
- **tensor、math 两张表**：语义只声明数值口径，`faults` 一个都没有（`operator_semantics.plan.toml`
  里那条断言 `tensor:matmul` 的 `faults` 是 `undefined` 就是钉这个状态的）。这些**这一轮没有核对实现**，
  这里只报表的状态，不当成“不出错”的证据。

`camel inspect --ops` 今天能看出两者的差别：空表打成 `"faults":[]`，没声明则整个成员不出现。读这条事实
的除了 `effects(g).may_fault`，还有把“可能出错”当成节点属性的那些地方（`effects.cpp#ofNodes`、
`definitions.cpp` 里对定义与算子声明的核对）；这张表怎么说，它们就怎么读。

## 为什么不能自己定

两种回答给下游的不是同一个答案：

- 若“没声明”是**第三态**（没人核对过，凡读数必须保守），那么它是有意义的，`pow` 家族内部一半一半、
  `mul_c` 与 `mul_i` 不一致这些就都是**已知缺口**，该记下来而不是消灭；谁想让它变成 `[]`，得带一份
  像这份清单一样的逐算子核对。
- 若表**只是没写完**，那么每一个“实现不出错却不声明”的算子都是一处待办，方向是全部补成 `[]`；
  代价是一次 800 多个算子的全量核对（`--ops` 列出 867 个，其中 831 个连语义记录都没有），
  并且一批图的 `may_fault` 会从 1 变 0，那是有后果的读数变化，不是注释。

两种都不能顺手做：补一个“看起来不会出错”的 `[]` 与留空一样是断言，只是方向相反。

## 选项

- **① 把表补完**：给每个实现不出错的算子写 `[]`，一次全量核对。**要多造**：800 多个算子的逐条核对
  （按这份清单的格式），以及判据——什么算“核对了”。**验收数**：清单行数从 77 涨到全量；一批图的
  `may_fault` 从 1 变 0，每一处都得有一条用例。
- **② 只把第三态写清楚**：在设计原则里补一句“`faults = []` 是核对过的断言，`nullopt` 是没核对过、
  必须按可能出错读”，并在 `operator_table.h` 的注释里点明这不是“未实现”。**要多造**：一句话，无代码。
  **验收数**：无行为变化；`pow` 家族与复数 `mul`/`neg` 的这些不一致作为已知缺口写进清单。
- **③ 只补裁定点过名、且已经核对过的**：`len`（三个 URI）、`pow` 的 i/l/f/d 与 c/z、复数 `mul`/`neg`，
  其余按 ② 处理。**要多造**：这几条的 `[]` 与各自的用例；②的那一句话。**验收数**：这几族的图上
  `may_fault` 变 0，一处一条用例；tensor/math 与其余 800 个仍按第三态读。

**已按裁定落地：①＋②＋③一起**。①那条读法没动（`traitsOf` 本来就是这么读的），③这批已经补完
（`len` 三个 URI、`pow` 十个、复数 `mul`/`neg` 四个），②的那句话落在
`docs/handover/reports/faults-table-audit.md` 的“按 reach 补的规矩”那一节里。裁定之前按 ② 读的
限制只剩 tensor/math 与其余没有语义记录的算子：它们仍然是“没核对过 ⇒ 可能出错”，
`declared_traits` 里 `tensor::sum` 那条就是钉这个状态的。