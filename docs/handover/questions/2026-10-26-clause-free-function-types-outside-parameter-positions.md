# 免子句的函数类型出现在**非形参位置**时，资源该按实例化定还是保持泛型？

**问谁**：语言作者（负责人）
**问什么**：《资源承诺的检查与 `when` 求值》那节的裁定是"**函数值的类型带上推断出的资源集**，没写子句时就用推断结果，不用 `Generic`；`Generic` 只留给'**函数类型的形参**不写资源'（对资源泛型、按实例化确定）那一种"。按这条落地之后，**非形参位置**的免子句函数类型（例如 `Map<int, (x: int) => int>` 里的回调类型、结构体字段里的函数类型）也按**实测子句**落成了具体类型，并且**进入了特化键**。请您定这是不是想要的读法。

## 量到的事实（本轮已在 develop 的落地上）

- **38 条"应当通过却被拒"全部清零**（改前逐字：`SemanticDiag::NoMatchingFunction` 拒绝 `<f: (item: T) => R> (xs: T[])` 与 `(x: int) => int pure` 相遇；改后接受）。机制是一条**方向性**关系：**免子句的一侧取另一侧的资源，其余差异一律严格**（替换了 8 处"书写类型 vs 值类型"的 `equals`）。反向不放松：`10_pure_callback_wrong.cml` 仍被拒；新案例 `map_callback_not_same_type.cml` 钉住"只放松资源、不通配别的"。
- **副作用**：值现在打印推断子句（`(x: int) => int pure`、`() => int uses stdout`），因此 32 条记录/金标随之重出（29 条"应当如此"、3 条形状变了）。
- **3 条形状变化里最值得您看的一条**（`test/cases/exemplars/05_effect_order`）：`run.log` 里**程序输出一字未变**，但 `simplified.agir.dot` 少了旧的同级 `visit$spec4`/`visit$spec8`/`visit$spec12`、多了**嵌套的** `visit$tspec2`，回调端口打印 `(x: int) => int uses stdout`。即：**非形参位置**的免子句函数类型现在带着实测子句参与特化键，特化的形状因此改变（功能等价，但"编译期分了几个实例"变了）。

## 需要您定的

- **甲**：这就是想要的（"免子句按实例化定"适用于**所有位置**，不只形参位置）⇒ 那么特化键带资源分量是**规则的一部分**，我们把它写进设计稿并保留现在的形状；请确认诊断与打印口径也照这个走。
- **乙**：只应**限于形参位置**（`Generic` 在那里保留、按实例化确定），**非形参位置**应保持某种更粗的形状（例如不带资源分量参与特化键）⇒ 请说明"更粗"具体是什么（是否仍要打印推断子句）。
- **丙**：为"免子句声明"保留一个**可见标记**（诊断里能区分"声明时没写"与"写的就是这个资源"）⇒ 请给出该标记在类型文本与诊断里的措辞。

## 现在的状态（不自行决定）

落地按**甲**的形状在跑（免子句一侧取另一侧资源、其余严格；非形参位置按实测子句特化），树是绿的（`-Check all` 8/8、`known` 未增）。**另**：按您的"小事"要求，函数体的度量已从诊断文件搬出——现在在 `src/compile/gir/builder_resources.cpp`（`footprintOf` / `measureInferredResources` / `checkInferredResources`），`builder_diagnostics.cpp` 只剩报告（-297 行、0 新增）。

## 证据位置（都在 develop 上）

- 关系实现：`sameTypeWithUnwrittenResources`（新增），替换 8 处 `equals`：`:mark/map_arr` 族 callback 参数、`:coll/map_put` 的 C++ value 类型、`:mark/apply_arr`、`:mark/filter_arr`、`:mark/foreach_arr`、`:macro/cmp`，另加 `CollectionType::assignableFrom/castSafetyFrom`、`resolveFaultIsolationType`、`unifyTypeVars` 的最弱解。
- 逐字 trace（改前）：`[RES-TRACE dyn:<key: K, value: V>] with=(int, (x: int) => int pure) norm=(Map<int, (x: int) => int>) res=<none>`。
- 形状变化用例：`test/cases/exemplars/05_effect_order`（`run.log` 不变、`simplified.agir.dot` 变化）；另两处为 `RESOURCES` 里 callee 访问改从自身子句读（`FUNC bump` 不再带 `result unknown`、`main` 的 CALL 不再单列访问，递归 `countdown` 仍 `unknown`）与 `boundaries` 槽位按 `normalize_node_ids` 重编号。