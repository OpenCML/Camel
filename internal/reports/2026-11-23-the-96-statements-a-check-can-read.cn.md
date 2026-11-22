# 96 条陈述写进了登记表,检查第一次能读到它们

- 分支:`lane/panic-declarations`,从 `9680035a4` 开
- 依据:`internal/reports/2026-11-22-the-registry-had-no-definition.cn.md` §五、§七 第 1 条
- 结论:96 项一项没变;**可读的陈述从 0 条变成 96 条**

## 一、读数

| 读数 | 改前 | 改后 |
|---|---|---|
| C++ 登记入口里检查能读到的 `panic` 陈述 | **0 条** | **96 条** |
| 其中指向已登记算子的 | 0 | 96(96 项全在) |
| 有可读 `panic` 陈述的已登记算子 | 213 / 382 | **214 / 382** |
| 检查的裁决 | 48 红、331 相符、3 条说了不抛 | 逐行相同 |

第三行只涨 1,是这一笔最该说清的数:96 项里有 95 项**此前就有一条可读的 Camel 陈述**
(`src/stdlib/prelude.cml` 91 处,测试用例 4 处),那条陈述没写 `panic`,检查按「没写就是不抛」
读作 `false`;真正一句陈述都没有的只有 `:coll/eq`。所以这一笔补的是 **C++ 入口那一半**:
两边都开口的只有一处,而 C++ 那一半此前一句也读不到。

## 二、96 项从哪来

`internal/reports/2026-11-22-the-registry-had-no-definition.cn.md` §五的原话:

> 读数:`total=729 same=729 changed=0 unstated_differs=96`

同一节把它写成一句话:「表说 `true` 的 633 个、说 `false` 的 96 个」。§七 第 1 条就是这一项。
这 96 项是故障表里**说「不会抛」**的全部条目:86 条在 `kNoFaultPath` 里(整数、浮点、复数、
盒装四种数值类型的 `+ - * neg`、比较、`eq`、`pow`,以及集合与结构的度量与判定),
8 条是复数类型 `add/sub/mul/neg` 只经 `declare()` 算出来的 URI,2 条是 `builtin.cpp` 里
`:op/extent_of` 与 `:op/copy` 的 `set(..., mayFault = false)`。

## 三、改了什么

1. `src/stdlib/std/builtin/executors/builtin_semantics.cpp`:一张
   `static constexpr CppPanicStatement kNoFaultOperators[]`,**96 行,每行一个完整的 URI 和一个
   字面量布尔**;循环把同一对交给两处 —— `declareFaults(uri, {})`(空故障表)和
   `CppRegistration(uri, panic)`(记录)。原来的 `kNoFaultPath`(86 个裸名字)与
   `stateNoFaultOperator(uri)`(只有变量、没有字面量)随之删除,`declare()` 里那条
   「空表就顺手写记录」的分支也一并去掉:陈述只剩一处。
   循环里还会把两处**读回来对一次**,不一致就拒绝加载并点名算子 —— 判据不再靠「两边应该一样」。
2. `src/stdlib/std/builtin/executors/builtin.h`、`builtin.cpp`:`stateNoFaultOperator` 的声明与
   两处调用删除(`:op/extent_of`、`:op/copy` 现在走那张表)。
3. `scripts/check-operator-panic.mjs`:C++ 入口此前用 `blank()` 之后的文本去匹配 —— 那个函数
   **把字符串字面量一并抹掉**,而这条入口的 URI 和布尔正是字面量,所以它从来没匹配到任何东西。
   现在改成只抹注释、保留字面量,并按**条目类型** `CppPanicStatement` 读表;表头认得出、条目读不出
   时退出 2,而不是读成「空陈述」。末尾多一行读数:「N of 382 registered operators state `panic`
   in a declaration this scan can read」。
4. `src/kernel/runtime/graph.cpp#is_chain_statement` 的 `Oper` 分支从 `traits->mayFault` 改问
   `recordOf(oper)->mayFault()`;没有记录的算子(内核 `set()` 写事实、不经登记的那些)仍读那条
   事实派生的特质,因为那是它唯一的陈述。

## 四、96 项逐项对照

两栏都是**同一项在源码里的答案**,不是跑出来的总数:「表的答案」是 `declareFaults(uri, {})`
之后 `traits.mayFault` 的值,「记录的答案」是 `CppRegistration(uri, false)` 之后
`Record::mayFault()` 的值。「改前这条陈述在哪」给的是那一项此前唯一可读的 `panic` 陈述所在
(它没写 `panic`,检查读作 `false`);`—` 表示此前一句也没有。

| # | 算子 | 改前(表) | 改后(记录) | 同 | 改前这条陈述在哪 |
|---|---|---|---|---|---|
| 1 | `:op/add_i` | false | false | ✓ | `test/cases/compile/native/when_shape_overlap.cml:29` |
| 2 | `:op/add_l` | false | false | ✓ | `test/cases/format/tags/tag_below_doc_comment.cml:8` |
| 3 | `:op/add_f` | false | false | ✓ | `src/stdlib/prelude.cml:84` |
| 4 | `:op/add_d` | false | false | ✓ | `src/stdlib/prelude.cml:89` |
| 5 | `:op/add_c` | false | false | ✓ | `src/stdlib/prelude.cml:126` |
| 6 | `:op/add_z` | false | false | ✓ | `src/stdlib/prelude.cml:130` |
| 7 | `:op/add_bf` | false | false | ✓ | `src/stdlib/prelude.cml:122` |
| 8 | `:op/add_f6` | false | false | ✓ | `src/stdlib/prelude.cml:118` |
| 9 | `:op/sub_i` | false | false | ✓ | `test/cases/compile/native/when_shape_overlap.cml:32` |
| 10 | `:op/sub_l` | false | false | ✓ | `test/cases/format/tags/tag_below_doc_comment.cml:19` |
| 11 | `:op/sub_f` | false | false | ✓ | `src/stdlib/prelude.cml:94` |
| 12 | `:op/sub_d` | false | false | ✓ | `src/stdlib/prelude.cml:99` |
| 13 | `:op/sub_c` | false | false | ✓ | `src/stdlib/prelude.cml:158` |
| 14 | `:op/sub_z` | false | false | ✓ | `src/stdlib/prelude.cml:162` |
| 15 | `:op/sub_bf` | false | false | ✓ | `src/stdlib/prelude.cml:154` |
| 16 | `:op/sub_f6` | false | false | ✓ | `src/stdlib/prelude.cml:150` |
| 17 | `:op/mul_i` | false | false | ✓ | `src/stdlib/prelude.cml:182` |
| 18 | `:op/mul_l` | false | false | ✓ | `src/stdlib/prelude.cml:186` |
| 19 | `:op/mul_f` | false | false | ✓ | `src/stdlib/prelude.cml:174` |
| 20 | `:op/mul_d` | false | false | ✓ | `src/stdlib/prelude.cml:178` |
| 21 | `:op/mul_c` | false | false | ✓ | `src/stdlib/prelude.cml:198` |
| 22 | `:op/mul_z` | false | false | ✓ | `src/stdlib/prelude.cml:202` |
| 23 | `:op/mul_bf` | false | false | ✓ | `src/stdlib/prelude.cml:194` |
| 24 | `:op/mul_f6` | false | false | ✓ | `src/stdlib/prelude.cml:190` |
| 25 | `:op/neg_i` | false | false | ✓ | `src/stdlib/prelude.cml:246` |
| 26 | `:op/neg_l` | false | false | ✓ | `src/stdlib/prelude.cml:250` |
| 27 | `:op/neg_f` | false | false | ✓ | `src/stdlib/prelude.cml:238` |
| 28 | `:op/neg_d` | false | false | ✓ | `src/stdlib/prelude.cml:242` |
| 29 | `:op/neg_c` | false | false | ✓ | `src/stdlib/prelude.cml:262` |
| 30 | `:op/neg_z` | false | false | ✓ | `src/stdlib/prelude.cml:266` |
| 31 | `:op/neg_bf` | false | false | ✓ | `src/stdlib/prelude.cml:258` |
| 32 | `:op/neg_f6` | false | false | ✓ | `src/stdlib/prelude.cml:254` |
| 33 | `:op/lt_i` | false | false | ✓ | `src/stdlib/prelude.cml:417` |
| 34 | `:op/lt_l` | false | false | ✓ | `src/stdlib/prelude.cml:421` |
| 35 | `:op/lt_f` | false | false | ✓ | `src/stdlib/prelude.cml:413` |
| 36 | `:op/lt_d` | false | false | ✓ | `src/stdlib/prelude.cml:409` |
| 37 | `:op/lt_s` | false | false | ✓ | `src/stdlib/prelude.cml:433` |
| 38 | `:op/lt_bf` | false | false | ✓ | `src/stdlib/prelude.cml:429` |
| 39 | `:op/lt_f6` | false | false | ✓ | `src/stdlib/prelude.cml:425` |
| 40 | `:op/le_i` | false | false | ✓ | `src/stdlib/prelude.cml:448` |
| 41 | `:op/le_l` | false | false | ✓ | `src/stdlib/prelude.cml:452` |
| 42 | `:op/le_f` | false | false | ✓ | `src/stdlib/prelude.cml:444` |
| 43 | `:op/le_d` | false | false | ✓ | `src/stdlib/prelude.cml:440` |
| 44 | `:op/le_s` | false | false | ✓ | `src/stdlib/prelude.cml:464` |
| 45 | `:op/le_bf` | false | false | ✓ | `src/stdlib/prelude.cml:460` |
| 46 | `:op/le_f6` | false | false | ✓ | `src/stdlib/prelude.cml:456` |
| 47 | `:op/gt_i` | false | false | ✓ | `src/stdlib/prelude.cml:479` |
| 48 | `:op/gt_l` | false | false | ✓ | `src/stdlib/prelude.cml:483` |
| 49 | `:op/gt_f` | false | false | ✓ | `src/stdlib/prelude.cml:475` |
| 50 | `:op/gt_d` | false | false | ✓ | `src/stdlib/prelude.cml:471` |
| 51 | `:op/gt_s` | false | false | ✓ | `src/stdlib/prelude.cml:495` |
| 52 | `:op/gt_bf` | false | false | ✓ | `src/stdlib/prelude.cml:491` |
| 53 | `:op/gt_f6` | false | false | ✓ | `src/stdlib/prelude.cml:487` |
| 54 | `:op/ge_i` | false | false | ✓ | `src/stdlib/prelude.cml:510` |
| 55 | `:op/ge_l` | false | false | ✓ | `src/stdlib/prelude.cml:514` |
| 56 | `:op/ge_f` | false | false | ✓ | `src/stdlib/prelude.cml:506` |
| 57 | `:op/ge_d` | false | false | ✓ | `src/stdlib/prelude.cml:502` |
| 58 | `:op/ge_s` | false | false | ✓ | `src/stdlib/prelude.cml:526` |
| 59 | `:op/ge_bf` | false | false | ✓ | `src/stdlib/prelude.cml:522` |
| 60 | `:op/ge_f6` | false | false | ✓ | `src/stdlib/prelude.cml:518` |
| 61 | `:op/eq_i` | false | false | ✓ | `src/stdlib/prelude.cml:362` |
| 62 | `:op/eq_l` | false | false | ✓ | `src/stdlib/prelude.cml:367` |
| 63 | `:op/eq_f` | false | false | ✓ | `src/stdlib/prelude.cml:357` |
| 64 | `:op/eq_d` | false | false | ✓ | `src/stdlib/prelude.cml:352` |
| 65 | `:op/eq_b` | false | false | ✓ | `src/stdlib/prelude.cml:402` |
| 66 | `:op/eq_s` | false | false | ✓ | `src/stdlib/prelude.cml:392` |
| 67 | `:op/eq_c` | false | false | ✓ | `src/stdlib/prelude.cml:382` |
| 68 | `:op/eq_z` | false | false | ✓ | `src/stdlib/prelude.cml:387` |
| 69 | `:op/pow_i` | false | false | ✓ | `src/stdlib/prelude.cml:542` |
| 70 | `:op/pow_l` | false | false | ✓ | `src/stdlib/prelude.cml:546` |
| 71 | `:op/pow_f` | false | false | ✓ | `src/stdlib/prelude.cml:538` |
| 72 | `:op/pow_d` | false | false | ✓ | `src/stdlib/prelude.cml:533` |
| 73 | `:op/pow_c` | false | false | ✓ | `src/stdlib/prelude.cml:558` |
| 74 | `:op/pow_c_i` | false | false | ✓ | `src/stdlib/prelude.cml:562` |
| 75 | `:op/pow_c_l` | false | false | ✓ | `src/stdlib/prelude.cml:566` |
| 76 | `:op/pow_z` | false | false | ✓ | `src/stdlib/prelude.cml:570` |
| 77 | `:op/pow_z_i` | false | false | ✓ | `src/stdlib/prelude.cml:574` |
| 78 | `:op/pow_z_l` | false | false | ✓ | `src/stdlib/prelude.cml:578` |
| 79 | `:op/pow_bf` | false | false | ✓ | `src/stdlib/prelude.cml:554` |
| 80 | `:op/pow_f6` | false | false | ✓ | `src/stdlib/prelude.cml:550` |
| 81 | `:op/and` | false | false | ✓ | `src/stdlib/prelude.cml:333` |
| 82 | `:op/or` | false | false | ✓ | `src/stdlib/prelude.cml:338` |
| 83 | `:op/not` | false | false | ✓ | `src/stdlib/prelude.cml:316` |
| 84 | `:op/extent_of` | false | false | ✓ | `src/stdlib/prelude.cml:946` |
| 85 | `:op/copy` | false | false | ✓ | `src/stdlib/prelude.cml:961` |
| 86 | `:struct/len_arr` | false | false | ✓ | `src/stdlib/prelude.cml:911` |
| 87 | `:struct/len_str` | false | false | ✓ | `src/stdlib/prelude.cml:905` |
| 88 | `:struct/len_bytes` | false | false | ✓ | `src/stdlib/prelude.cml:918` |
| 89 | `:struct/contains_arr` | false | false | ✓ | `src/stdlib/prelude.cml:1077` |
| 90 | `:struct/contains_str` | false | false | ✓ | `src/stdlib/prelude.cml:1084` |
| 91 | `:coll/len` | false | false | ✓ | `src/stdlib/prelude.cml:1179` |
| 92 | `:coll/has` | false | false | ✓ | `src/stdlib/prelude.cml:1211` |
| 93 | `:coll/set_contains` | false | false | ✓ | `src/stdlib/prelude.cml:1244` |
| 94 | `:coll/eq` | absent | false | ✗ | — |
| 95 | `:coll/set_is_subset` | false | false | ✓ | `src/stdlib/prelude.cml:1271` |
| 96 | `:op/eq_bytes` | false | false | ✓ | `src/stdlib/prelude.cml:397` |

**96 行,0 行 `✗`。**

## 五、为什么一项都没变

96 项在改前就已经在**运行期**被写进记录了(`stateNoFaultOperator` 里那句
`CppRegistration(uri, false)`),所以两个读者在改前就同答案。这一笔没有给任何算子一个新的答案:
它把那个答案从「循环里的变量」搬到「表里的字面量」,于是检查能数它。数字没变不等于东西没变 ——
变的是可数性(0 → 96)、覆盖面(`:coll/eq` 此前一句陈述都没有)和一个读者(`is_chain_statement`
现在读记录),答案本身一处没动。

## 六、余下的

- 382 个已登记算子里,168 个没有任何可读的 `panic` 陈述:47 个实现会抛(检查判红,红得对),
  121 个不抛却没人说。这 168 个不在这一笔里。
- 3 条「写了 `panic` 却不抛」(`:op/cast`、`:op/access`、`:op/access_key`)仍在,没动。
- `:attr/*` 一类由内核 `set()` 写事实、从不登记的算子没有记录,`is_chain_statement` 对它们
  仍读特质;把它们也搬进记录是另一笔。

## 七、验证

- 本地:`check-operator-panic.mjs` 改前/改后输出逐行比对,只多出第三节那一行读数,裁决全同;
  `check-doc-links` 62 篇 0 断链;`conflict-markers` 0。
- 远程(`internal/tools/camel-remote.ps1 -Plan test/plans/smoke,test/plans/feat/opt,test/plans/feat/compile,test/plans/feat/modules,test/plans/feat/linear -Jobs 8`):
  `[SUMMARY] total: 7246, pass: 7217, fail: 6, skip: 9, known failures: 14`。6 红是
  `docs.planted_page_is_red`、`docs.planted_page_passes_the_page_rule`、`docs.word_form_only`、
  `plan_validation`、`opt.writings.greedy_fusion_reads_no_writing`、
  `opt.writings.two_effects_wall_clock`;`internal/lanes.md` 里 `lane/stage2-graph` 一行记的
  「`docs.*`、`opt.writings`、`plan_validation` 在 `develop` 上同红」正是这一批。

## 八、提交

- `1c9fc6398` ops: state the 96 no-fault operators as literals a check can read
- `807383573` graph: read a chain statement's fault answer from the operator record