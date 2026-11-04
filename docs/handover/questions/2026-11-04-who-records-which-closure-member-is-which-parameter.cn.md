# 闭包成员与参数的对应，由谁记录、经哪条通道

**状态**：待裁；我方默认见第 5 节（**在闭包端口节点上记一个 Int 属性 `transforms.parameter_of`，`split` 写、`transpose` 读；读不到就保持今天的明确拒绝**）。本轮没做，原因见第 4 节：**记录已经有了，缺的是把它运到 `transpose` 的通道**。

## 1. 问题

`transpose` 的 `forward` 在 Port 分支回答"这个参数的值是多少"时，`portOfParam[at]` 是空的（participating 的参数按裁定不建端口）。它要的值在闭包里，闭包里的成员也确实都建了端口——**但没有任何一处记录"第几个闭包成员是第几个参数的值"**，所以 `forward` 只能拒绝。

今天的三对齐打印（`modules/transforms/transpose.cpp#knownValue` 的拒绝处，跑完已撤）：

```
ALIGN key=3 kind=1 type=float[2, 2] closureMember=0 paramPort=2 withPort=0
  closureOf(3): 0[k=1,t=float[2, 2],p=0] 1[k=1,t=float[2, 2],p=1] 2[k=1,t=float[2, 2],p=2]
  portOfParam(2): 0->4294967295 1->4294967295
  normPorts(2): 0:ref3[at=0] 1:ref4[at=1]
  closureNodes(3): 0[ported] 1[ported] 2[ported]
```

取值键是**参数端口**（`ref3` = `normPorts[0]`），参数**没有端口**，值**在闭包里**（三个成员都 ported），**两者之间没有一条边**。

## 2. 记录其实已经存在

`split` 建闭合时就记了：`modules/transforms/split.h#Residual::parameter`（"The parameter it is the value of, when it is a parameter; `kNotAParameter` when it is a value the forward half computes"），而 `modules/transforms/split.cpp#residualsOf` 的编号规则是"**参数按参数下标在前，计算值按节点在后**"。也就是说：

> `residuals[i].parameter == k` ⟺ 闭包第 `i` 个成员是参数 `k` 的值。

`split` 建闭包端口时（`split.cpp#addResidualCaptures` / `#buildLinearClosure`）手上就有这张表。**所以不是"没人记录"，是"记录了没运到 `transpose`"。**

## 3. 禁掉的两条（记下来，防止后人再走）

- **按类型找**：`linear` 的两个参数**类型完全相同**（都是 `float[2, 2]`），"取第一个同类型成员"会**不报错、给错数**——比今天的明确拒绝更坏。
- **按位置猜**（"参数 `at` 就取闭包第 `at` 个成员"）：只有在"每个参数都被后一半读到、且在闭包里连续排在最前"时才成立；一个参数读不到时下标就错位，**同样是静默给错数**。

## 4. 缺的是通道（本轮实测）

`transpose` 是另一个 pass，它手上只有图，没有 `split` 的 `std::vector<Residual>`；两条路都不通：

- `TransposeState`（`transpose.cpp` 内的结构）与 `split` 的 `Half` 不共享任何东西；
- `GCGraph` 已有的 per-graph 通道只有 `captureNames()`（`SymId` 序列），它记的是**名字**不是**参数下标**，拿名字去配参数名是**命名约定**，不是事实。

而 `camel/core/node_attrs.h` 本来就有 per-node 属性（`Int` 值、"graph content"、随重写复制），**它就是为这种事准备的通道**。所以缺的不是机制，是把这条通道用起来的那一次改动：注册一个键、`split` 写、`transpose` 读。

## 5. 默认（我方按此做，除非被推翻）

1. 在 `camel/core/node_attrs.h` 的既有机制上注册一个键 `transforms.parameter_of`（`Int`），值 = 参数下标 + 1（0 留给"没有"）。
2. `split` 建闭包端口时，对 `residuals[i].parameter != kNotAParameter` 的成员写这个属性。
3. `transpose.cpp#forward` 的 Port 分支：`portOfParam[at]` 为空时，**在闭包成员里找属性等于 `at` 的那一个**，找到就返回它的端口；**找不到就保持今天的拒绝**（一个字都不按类型或位置猜）。
4. 配套一条反例（作者要求）：同类型两参数、**取不同的值**（`a` 全 1、`b` 全 2），要求 `dL/da` 用 `b` 的值、`dL/db` 用 `a` 的值，两个答案都与手算一致——**若解析走了"第一个同类型成员"，两个答案会变成同一个数，这条用例就能抓出来。**

## 6. 为什么这值得一问而不是直接做

它给**所有**律加了一条事实来源（图上多一列属性），会进"graph content"、进哈希、进导出；而这条通道今天在图里是空的。作者对"图里记什么"一直有明确裁定（属性是"库挂上去的内容"，键由库注册），所以这一步的形状（键名、值域、是否随重写复制）应当由他一句话定，而不是我替他选。