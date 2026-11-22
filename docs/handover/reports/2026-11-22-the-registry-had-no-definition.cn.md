# `Registry` 的零定义：清点、补写、以及「`mayFault` 一项不变」的表

本页是 registry 车道（`lane/registry`，基线 `develop = cdd3639ef`）的读数。每节对应派工的一步，数字都是在树上量出来的。

## 一、① 零定义边界（`cdd3639ef` 上的读数）

`Registry` 的 11 个成员 + `Record` 的 2 个读数，在基线上一共只有声明，**没有任何定义，也没有任何调用点**——所以它不是链接期问题，而是「一个没人读的登记」：

```powershell
git grep -nE "^[A-Za-z_].*[^A-Za-z_]Registry::(instance|intern|lookup|find|bind|size|declare|registerOperator|complete|epoch|kernel)\b" cdd3639ef
# 0 行：全树没有一处定义
git grep -n "camel::ops::Registry" cdd3639ef
# 3 行，全部是注释与文档：operator_record.h:543、kernel/runtime/graph.cpp:95、本目录的旧报告
```

| 符号 | 声明位置 | 基线出现次数 | 定义 | 调用点 |
|---|---|---|---|---|
| `Registry::instance` | `operator_record.h:467` | 63（**全部**是 `DerivativeRegistry`/`NodeRoleRegistry`/`LoweringRegistry`/`OpRegistry` 等别的登记处） | 0 | 0 |
| `intern`（两个重载） | `:472`、`:474` | 0 | 0 | 0 |
| `lookup`（两个重载） | `:477`、`:478` | 0 | 0 | 0 |
| `find`（两个重载） | `:482`、`:483` | 9（全部是 `DerivativeRegistry::findRule`、`LoweringRegistry::find`、`OpRegistry::find` 等） | 0 | 0 |
| `bind` | `:487` | 1（`operator_record.h:74` 的散文） | 0 | 0 |
| `size` | `:490` | 0 | 0 | 0 |
| `declare` | `:499` | 0 | 0 | 0 |
| `registerOperator` | `:507` | 2（注释） | 0 | 0 |
| `complete` | `:521` | 2（注释） | 0 | 0 |
| `epoch` | `:525` | 0 | 0 | 0 |
| `Record::effect` | `:443` | 1（注释） | 0 | 0 |
| `Record::external` | `:453` | 0 | 0 | 0 |
| `CppRegistration::kernel(void *)` | `:599` | 1（声明） | 0 | 0 |

**没有「有人调而无定义」的情形**：`kernel/runtime/graph.cpp:95` 只是注释里写它将来要读 `mayFault()`，今天 `nodeMayFault` 走的是 `OperatorTable::traitsOf`。`git grep -c "CppRegistration" -- lib` = 0，登记侧一个写入者也没有。

## 二、② 写出来的定义：`kernel/ops/operator_registry.cpp`

文件头写清了它补的是什么、为什么、以及它不解决的三个问题。要点：

- 记录在一个函数内的 `Store` 里，**不是**旁表：没有第二份副本，所有读者只经过 `find`/`lookup`/`kernel`。
- `find` 是**无锁**的（两级定长原子表：4096 槽一块、1024 块，共 4,194,304 个算子；记录只发布一次、永不释放，所以「指针活到进程结束」成立）。名字表与所有写入走一把 `mutex`。
- `declare`/`registerOperator` 把一份陈述并进记录：别的入口已经说过的项，先比再并；不等就 `std::invalid_argument` 命名算子、项和两份陈述。`Stated<T>` 记住是谁说的，「没人说」保留自己的答案。
- `complete` 做两项「一个事实有两份陈述」的检查（逐元素位 vs 逐元素成立的代数律；原位形式 vs 它的普通算子），把原位形式自己的那条记录填上 `plain`，然后移动 `epoch`；已发布的记录拒绝后来的陈述。
- `Record::effect()`/`external()` 只读 `uses`：没人写子句是 `Undeclared`/触及程序之外；空子句是 `Pure`；只有程序自己的变量是 `StateOnly`。

**接口只加不改**（作者裁定），两处新增都标了「待审」，都写在头文件里：

1. `Registry::kernel(OperId) const`——`kernel(void *)` 那条路唯一的读者（见第三节）。
2. `CppRegistration` 的私有字段 `kernel_` 与 `friend class Registry`——`kernel(void *)` 的设置函数写它，`registerOperator` 读它。

不可比较的项（`TypeFolder`、`InstantiateFn`、`AttrGrant::value`、`CallSpec::checkFormat`、`BinaryLaws::elementOf`；类型规则是 `shared_ptr`，只按指针比）按「先说的站住、后来的不拒」处理，并在文件头标了待审：要不要给它们一个可比较的写法，是接口评审的问题，这一批不定。

## 三、③ 把 kernel 交给 `Registry` 的路

`kernel` 不是记录项（头文件原话：「a target registers its own implementations against the same OperId and they do not enter the record」），所以它不进 `Record`，而是**存在登记处里、按 OperId 一格**：

```
CppRegistration::kernel(void *)  →  kernel_（新增私有字段，第二次写同一个项按重复陈述拒绝）
Registry::registerOperator(reg)  →  mergeRecord(reg.stated()) + slot.kernel.store(reg.kernel_)
Registry::kernel(OperId)         →  读者（自检用例读它，两次登记读回两个不同的指针）
```

这是登记处自己的存储，不是旁表：写它的只有 `registerOperator`，读它的只有 `kernel(OperId)`，没有第二处。

## 四、④ 登记侧说出 `panic` 的用例

- 用例：`tools/oprecord/main.cpp`（`camel-oprecord --self-check`，工具只给测试用，已进 `scripts/pypi-rollup.js` 的 `TEST_ONLY_TOOLS`）
- 计划：`test/plans/feat/modules/op_record.plan.toml`（`tier = "fast"`，一条）

路径是一条直路：`intern` → 没人写 `panic` 时 `mayFault()` 是保守的 `true` → `CppRegistration(":test/copy", false)` + `kernel(&kernelA)` → `registerOperator` → `find(":test/copy")->mayFault()` **是 `false`**，且记录说这句话是 C++ 入口说的 → `Registry::kernel(id)` 交回同一个指针。

主机 `88zjwei` 上的 `[PASS]` 原话（计划运行器）：

```
[PLAN] feat.modules.op_record: modules.op_record
[PASS] op_record.cpp_registration_states_panic               43.02 ms
[SUMMARY] total: 1, pass: 1, fail: 0
```

用例自己的 16 行 `[PASS]` 里，`panic` 这一项的三行是：

```
[PASS] a record nobody stated `panic` for reads `mayFault() == true`: nobody said, so a call may fail
[PASS] the registration's second argument reaches the record: `mayFault()` is false where it was the conservative true before
[PASS] a Camel declaration stating `panic = true` where the C++ entrance stated false is refused, naming the item
```

## 五、⑤ 每个算子的 `mayFault`：改前改后

怎么量的（两条命令，主机上跑）：

```bash
CAMEL_HOME=out/latest out/latest/bin/camel inspect --ops test/cases/modules/stdlib/registry_all.cml   # 810 行 OPERATOR，729 个不同 URI
out/latest/bin/camel-oprecord --table <「may_fault<TAB>uri」逐行>                                       # 每个算子三个读数
```

`--table` 对每个算子给出三个读数，判据是「记录的 `mayFault()` 与表的 `may_fault` 是否相同」：

- `table`：今天 `nodeMayFault` 读的那一项（`OperatorTable` 的 `mayFault`）；
- `unstated`：记录在**今天**的写入者下的读数（`!panic.stated() || panic.value`，今天没人写 → `true`）；
- `stated`：把表里的值经**声明入口**写成 `panic` 之后，记录的读数——这一列必须与 `table` 逐项相同。

**读数：`total=729 same=729 changed=0 unstated_differs=96`。**

| 面板 | 读数 | 说明 |
|---|---|---|
| A 记录把表的读数逐项搬过来 | **729 / 729 相同，0 项变化** | 表说 `true` 的 633 个、说 `false` 的 96 个，记录答的每一个都和表一样：这条读路没有丢项、没有串键、没有翻默认 |
| B 今天的写入者（零个） | **96 项会变（`false` → `true`）** | 今天改 `nodeMayFault` 会变的正是这 96 个 |

面板 B 的 96 个（表说「不会故障」，记录说「没人说」）：

```
:op/   copy extent_of not or and
       add_{bf,c,d,f,f6,i,l,z} sub_{bf,c,d,f,f6,i,l,z} mul_{bf,c,d,f,f6,i,l,z} neg_{bf,c,d,f,f6,i,l,z}
       eq_{b,bytes,c,d,f,i,l,s,z} ge_{bf,d,f,f6,i,l,s} gt_{bf,d,f,f6,i,l,s} le_{bf,d,f,f6,i,l,s} lt_{bf,d,f,f6,i,l,s}
       pow_{bf,c,c_i,c_l,d,f,f6,i,l,z,z_i,z_l}
:coll/ eq has len set_contains set_is_subset
:struct/ contains_arr contains_str len_arr len_bytes len_str
```

**结论：`nodeMayFault` 可以改（面板 A 说明记录这条路是忠实的），但今天还不能改**——改之前这 96 个算子必须先把 `panic` 写出来（每一条都是 `false`：「不会故障」），否则那 96 个会从 `false` 变 `true`，那是行为变化。这一批没改 `graph.cpp`，也没改任何一个算子的声明。

## 六、⑥ `node scripts/check-operator-panic.mjs`

改后（`lane/registry` 的 tip 上）：**`48 operators raise and state no `panic`; 330 agree; 0 state a fault their implementation does not raise`**，exit 1——与改前逐字相同，一个算子都没动。

## 七、留给下一批的

1. 那 96 个算子写 `panic = false`（C++ 侧经 `CppRegistration` 的第二个参数，或经声明）；写完 `nodeMayFault` 的每一臂才能改成读记录。这一批只报「可以改」，不改。
2. `find` 今天不检查 `completed`；「已发布」只用来拒绝后来的陈述。谁先需要读「完成」这件事，谁再加。
3. `complete` 说的「两个入口的 `panic` 互比」在合并时就做完了（一项、一个值，不等即拒），完成时没有第二条可比的陈述；「纯」与访问表也是同一项，不互推。
4. 版本：`intern` 保留它第一次拿到的 key；另一个 major 的陈述会被并进同一条记录。`bind` 是唯一比版本的读者。这一条记在文件头，没在这一批定。