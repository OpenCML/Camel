# 作者 2026-11-03 的四节裁定，落在哪、哪些还是活

底：`origin/fsh-dev` = `dc4f17190`（`origin/develop` = `d7f15f2b3` + 8 笔），本车道 `lane-fsh1` 以 `--ff-only` 合入，无合并提交。
读的是 `docs/handover/reviews/questions-2026-10-29.md` 末尾四节（+49 行）与被挪进 `reviews/` 的两页（R100）。
每行：**时间戳 / 一句话 / 落点（车道或 `file#Symbol`）/ 现在是否是我们的活**。

## 1. 已遵守、已落地的（不列待办）

| 时间戳 | 裁定一句话 | 落点 | 是我们的活？ |
| --- | --- | --- | --- |
| 18:35 | 归档页是当时的记录，不改写：行号过时是正常的，"按符号引用不写行号"只管今后新写的页与仍在维护的设计文档 | 全车道的文档纪律。引用改写在这一批里还有五笔落在 11-04 02:13–02:21（`9f5125a3d`、`0810f6d70`、`fb9d8b01b`、`1366394b3`、`ec570569a`），此后没有新的引用改写；按"已经改的不用回滚"不回滚 | 否 |
| 18:35 | 闸门入口进 `internal/tools/`（第 6 项，照 12:30、13:30 做的） | `internal/tools/`：`d96cbfc68`、`46a0db2ee` | 否 |
| 18:35 | 账本改成名单 | `31e76298e`、`adbc7a2d2` | 否 |
| 18:35 | 第 6 项不该排在 1–3 前面；顺序是 ① 规则层把转置要的原值信息作为已知实参交出、再删按位置取值 ② 嵌套调用把被调函数的 `lin` 作为外层残差、转置时 `transpose(lin_g)` ③ `extent_of` | ①`d1aa176ee` + `34b659107`；②`db741501d`；③`7ea644415` | 否 |
| 18:55 | 安装自带 C++ 运行时（A）：`$ORIGIN` 运行路径 + `collect-out` 把工具链的 `libstdc++` 拷进 `libs/`，系统目录里的不拷 | `CMakeLists.txt`（`BUILD_RPATH`/`INSTALL_RPATH` = `$ORIGIN;$ORIGIN/../libs`）、`scripts/collect-out.js#findCxxRuntimeLibrary`、`scripts/collect-out.js#isSystemLibraryDir`；`26888783c` | 否 |
| 18:55 | C 也做：加载时版本不够，打一句明确的话再退出，不让加载器抛一串 `GLIBCXX_* not found` | `scripts/cxx-runtime-guard.mjs#installCxxRuntimeGuard`、`scripts/cxx-runtime-guard.mjs#CXX_RUNTIME_LIBRARY`；`338fd5024`（合并 `0e2ca326f`） | 否 |
| 19:35 | 第一步只改下标这一对规则：加线性函数 `pick_at`、加 `pick_at_t`，`jvp_idx_arr` 右边改成 `(dxs) => pick_at(dxs, i, len(xs))` | `stdlib/rulesets/jvp.cml#pick_at`、`stdlib/rulesets/transpose.cml#pick_at_t`；`d1aa176ee` | 否 |
| 19:35 | 第二步把其余读线性操作数形状的规则逐条改掉（`take_t`、`mean_axis_t`、`sum_t`） | `d1aa176ee` 一次改完；判据是命中数归零、用例不变 | 否 |
| 19:35 | 第三步：命中归零后删掉按位置取值那段、`wantedValues`/`TRANSPOSE-NEEDS`、`split.cpp` 的约定注释 | `modules/transforms/transpose.cpp#buildTransposeMember`；`34b659107`；代码里 `wantedValues`、`closurePorts[` 各 0 处 | 否 |
| 19:35 | 裸 `transpose` 上的 `idx_arr_t` 不走这条路，长度由 `extent_of` 从类型取 | `extent_of::<A>()`；`7ea644415` | 否 |
| 19:55 | 还差的两处注释（讲"第 `at` 格是第 `at` 个参数"）删掉 | `modules/transforms/transpose.cpp`、`modules/transforms/split.cpp`；`34b659107` | 否 |
| 19:55 | 闭包成员是已知值（捕获的常量），不是线性输入，没有余切可收——乙，不收；`wantsCotangent` 对闭包端口答 `false` | `modules/transforms/transpose.cpp#buildTransposeMember` 的 `wantsCotangent`；`bbfc003ec`（合并 `a8fa16870`） | 否 |
| 19:55 | 七条从答对变拒绝的成因（裁定说是 `forward` 不处理闭包端口）**前提没成立**：`forward` 的闭包端口转发早已在 `236d06f37`，真正缺的是 `reachesAParticipant` 只走 normal 列表 | `modules/transforms/transpose.cpp#reachesAParticipant`（走两张输入列表）；`bbfc003ec`；实测见 `docs/handover/questions/2026-11-04-who-states-the-extents-a-product-law-reads.cn.md` | 否 |
| 19:55 | 两条元组用例静默变 0 ⇒ 有依赖线性输入的值被放进了闭包成员，要逐个打出来找到并修 | 成因不是"塞进闭包成员"：`Fill` 把元组成员放在 `<>` 输入里，而 walk 只走了 normal 列表，元组件的余切被丢掉；`bbfc003ec` | 否（缺陷已闭合，不必另开排查） |
| 19:55 | 修好后两条照旧答 `(3.0)` 与 `(1.0)/(3.0)`，且 `wantsCotangent` 对闭包端口答 `false` | `test/plans/feat/compile/transforms.plan.toml` 的 `transpose_tuple_component.two_reads_of_one_tuple_add`、`transpose_no_tangent.tuple_member`；读数见本车道闸门 | 否 |
| 19:55 | 页里那个"阈值"不用写：这条规则不随用例数改变 | `docs/handover/reviews/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md` §3 保留为当时的默认，页已归档 | 否 |

## 2. 真正没做的（待办）

| 时间戳 | 裁定一句话 | 落点 | 是我们的活？ |
| --- | --- | --- | --- |
| 19:55 | 七条里 1 条回到原数，另 6 条往前一步停在 `skip=cotangent-needs-a-value uri=tensor:multiply`：律读的是它正给余切的那个切向操作数的形状，而 `float[..]` 不写形状 | `stdlib/rulesets/transpose.cml#mul_tensor_t`（由规则把长度写出来）；待裁页 `docs/handover/questions/2026-11-04-who-states-the-extents-a-product-law-reads.cn.md` 默认甲 | 是，但不在本车道：要另开一条 `stdlib` 车道（`lane-batch3`、`lane-chainplace` 都不做它） |
| 18:55 | 发布 wheel 时照 A 带上运行时，并在发布说明里写明体积变化 | 发布车道（`changes.log`、发布说明）；体积已量到并记在 `docs/handover/pitfalls.cn.md` 的 GLIBCXX 行（wheel 约 +8 MB 压缩后） | 是，发布时才做 |
| 19:35 | 用例的 `desc` 仍在描述一条已经不再打印的 `TRANSPOSE-NEEDS ... param=0` | `test/plans/feat/compile/transforms.plan.toml` 的六条 `desc`；`modules/` 里 0 处 | 是（文案陈旧，不是判据） |

## 3. 两页挪进 `reviews/` 之后

- 两页 R100 挪走（`dc4f17190`），内容一字未改：作者挪页就是答复，页本身不再改写。
- `docs/handover/questions/README.md` 由 `node scripts/check-doc-links.mjs --questions-index --write` 重建：**48 页**，59 行 = 48 行表格 + 11 行页头，没有手写、没有把页挪回来；`docs.links.questions_index` 绿。