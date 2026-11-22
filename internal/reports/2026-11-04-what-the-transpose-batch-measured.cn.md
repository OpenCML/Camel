# 这一批 transpose 改动量到了什么（2026-11-04）

**树**：`D:\Projects\Camel-wt\withport2`（分支 `lane-withport2`，`d7f15f2b3` + 本批改动，提交 `27a5523b5`/`9b4e38004`）
**宿主**：`/home/zjwei/camel-work/withport2`（`CAMEL_HOME=$PWD/out/latest`）
**时间**：2026-11-03 23:18 – 2026-11-04 13:36（北京时间；宿主在 EST，宿主钟 ≈ 北京 − 13 h）
**负载**：1m load 22–26（同一台机器上另有五条车道在排队，构建/测试各有一把 `flock`）

`agents/test.md` 的“Notes and comments hold the criterion, never the measurement”落地之后，`desc` 与代码注释里的读数都收到这一页。下面每一条都是**一棵树、一台机器、一个时刻**的事实，不是判据。

## 1. `mk(x)(2.0)`：按内容读完仍是拒绝

用例 `test/cases/compile/transforms/transpose_callee_is_a_returned_function.cml`，`-v`，`std::nvm`，二进制 `2026-11-03T23:18:19-05:00`。

改前（`calleeIdOf` 读 `with` 输入）：

```
TRANSPOSE function=mk skip=signature
TRANSPOSE function=f skip=call-no-transpose node=3 callee=mk
```

改后（`GCGraph::staticFunctionGraphOf` 读同一个输入）：

```
TRANSPOSE function=f skip=call-no-transpose node=3 callee=none value=2:0 with=1:10 reason=not-a-data-node
```

两次都是前向 `6.0` 之后 exit 1，转置不给数。改后的句子：

```
transforms: transpose has no transpose for this function: this call's callee has no transpose of its own, so the call cannot be turned around.
```

## 2. `jvp` 那句“yet”诊断

用例 `test/cases/compile/transforms/jvp_loop_iterable_tangent.cml`，`-v`，`std::nvm`。

改前：

```
error[RuntimeDiag::RuntimeError]: … transforms: jvp does not reinterpret a loop over a value that has a tangent
```

改后：

```
error[RuntimeDiag::RuntimeError]: … transforms: jvp cannot differentiate through a loop over a value that has a tangent (the element the loop hands its body has the tangent of the array's element, and no tangent of an element is carried).
```

同一台机器上 `test/cases/compile/transforms/library_grad_of_a_with_weight.cml` 改前改后逐字相同：

```
weighted engine=3.0 value=9.0 grad=3.0
```

## 3. 端口类型比较：先开火两次，然后被限到它声称要测的那个量

第一版比较不看种类。`test/plans/feat/compile/transforms.plan.toml` 的一次完整运行（`-tier fast -Jobs 4`）因此红了两条：

```
[FAIL] mean_axis1_keepdims_gradient_reads_the_row_length.nvm
[FAIL] mean_axis_negative_axis.through_the_library.nvm
```

两条都答对数字，被新判据拒了。句子里的两个拼写还都是空的：

```
transforms: transpose has no transpose for this function: the rule for '' takes its cotangent as `` and this step hands it a ``, and the language converts between those types nowhere
```

原因记在案：`refuse(...)` 把整条记录重写成 `{cause, param}`，`portType`/`handedType` 先写进去就被清空 —— 改成先 `refuse` 再填两个字段。

限到同种之内（两个数组 / 两个数）之后，两条回到正确的数：

```
mean_axis1 keepdims value=120.0 cotangent=[[3.3333333333333335, …], [6.666666666666667, …]]
mean_axis_negative library value=7.00000000000000000 d=[[0.3333333333333333, …], …]
```

**这件事的定性**：这不是“放宽判据让红的变绿”，是**把判据限到它声称要测的那个量上**。在 对 > 明确拒绝 > 错数 > 崩 的次序下，**把对的判成拒是降档**，不是保守。

## 4. 这个检查现在从任何程序都到达不了

两条路都不通：

- **程序侧**：`transpose(f)` 交给用户的类型是 `(ct: Tangent<ResultOf<F>>) => …`（`stdlib/types.cml#Transpose`），所以程序能递进来的余切类型只有 `Tangent<ResultOf<F>>` 一个；端口与它不符，靠写程序写不出来。
- **树侧**：`-tier fast -Jobs 4` 跑完 `test/plans/feat/compile/transforms.plan.toml`（170 例）与 `test/plans/feat`（8762 例），没有一条走到这句话；两个 `matmul_t` 基线红更早停在 `skip=cotangent-needs-a-value`。

所以它按作者的第二次裁定写成 `internal error: … a compiler defect and not the program's fault`，**不钉用例**：一条没有任何程序能到达的判据不是判据。

## 5. 五道闸（同一棵树，逐条单目标）

| 闸 | 读数 | 时间 |
|---|---|---|
| `test/plans/feat/transforms` | `total 45, pass 37, fail 0, known failures 8` | 13:01 |
| `test/plans/feat/compile` | `total 2332, pass 2313, fail 0, known failures 19` | 13:09 |
| `test/plans/feat/tools` | `total 58, pass 58, fail 0` | 13:18 |
| `test/plans/feat/docs_links.plan.toml` | `total 21, pass 21, fail 0` | 13:28 |
| `test/plans/feat` | `total 8762, pass 8719, fail 3, skip 11, known failures 29`，exit 1 | 13:36 |

三个红逐字是基线那三个：

```
[FAIL] matmul_t.vector_lhs_law.nvm
[FAIL] matmul_t.vector_rhs_law.nvm
[FAIL] matmul_t.rank_three_is_refused_by_name.nvm
```

## 6. 引文与文档

`node scripts/check-doc-links.mjs --citations` → `813 of 813 symbol citation(s) verified`（合并 `develop` 前）；合并后 817/817。
`node scripts/law-cotangent-ports.mjs` → `137 law(s) in 5 file(s), 170 port(s), 9 with no witness`（脚本未改动）。
`node scripts/diagnostics-catalog.mjs --check` → `311 codes`，无重复（本批未新增诊断码）。