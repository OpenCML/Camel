# 一个 struct 字段读（`qq.a`）：切里按名字拒，还是把 key 抄过去

## 问句

`split` 的两半抄一个「按名字读 struct 成员」的 `Accs` 时，**本轮按名字拒绝**（已落地），还是**把 key 抄过去**
让它切得动（约两行）？

## 现在的状态（树 `splitports`，base `44300579c`，2026-11-04 03:4x +08:00，主机 load 18–48）

原来的落点是**崩**：`split(jvp((qq: Point) => qq.a * 4.0 + qq.b))(p)` 在 `db741501d` 原样上就死在
`nodevm.cpp` 的 `idx < t->size()`（`Tuple index out of bounds`），而 `jvp` 单独跑是
`lift value= 11.0 tangent= 4.0` ——**抬升是对的，是切丢的**。

本 lane 把它改成按名字拒绝（`modules/transforms/split.cpp#makeCopy` 的 `GCNodeKind::Accs` 一格），句子与位置：

```
transforms: split does not copy a field of a struct, read by name (`b`) yet (the cut copies a member
  read by its **place** in a tuple, and a struct's member is read by its **name**: ...)
   --> test/cases/transforms/split/split_struct_field_read.cml:17:8
note: raised here
   --> ...:26:15   let fwd = split(lifted)
```

**「struct 端口切不动」这个定位是错的，本轮量了三张图**：

| 形状 | 程序 | 读数 |
| --- | --- | --- |
| struct 端口**原样穿过**（不读成员） | `g(qq: Point) = qq` | 切得动：`forward y= { a: 2.0, b: 3.0 }`、`lin dp= { a: 1.0, b: 0.0 }`、`residuals=1` |
| 同形状写成**元组**再按下标读 | `type Point = (float, float)`，`qq.0 * 4.0 + qq.1` | 切得动：`forward y= 11.0`、`lin dp= 4.0` |
| struct 端口**读成员**（按名字） | `g(qq: Point) = qq.a * 4.0 + qq.b` | 原来崩，现在拒绝（上面那句） |

所以 `Tangent<Point>` 是逐成员切线这件事，切里**本来就是成立的**：`valuePortsOf` 拿前半、`tangentArgumentsOf`
拿后半，两端都按端口走，一个 struct 端口就是一对端口。丢图的是**读**，不是端口 —— 抄 `Accs` 时只写了
`addAccsNode(type, body->value, 0)`（按下标），payload 里的 `key` 被丢掉，运行时把 `Struct` 当 `Tuple` 索引。

## 三条路与代价

- **甲（本轮默认，已落地）**：在读到的那一格按名字拒绝，句子说清「名字不是任何一半编号的位置」。代价：
  任何读 struct 成员的体都切不了 —— `Point`/结构体算术是常见形状，这一条拒得比较宽。好处：一句话说得出，
  错数、崩都排除，且**不发明**切线端的行为。
- **乙**：把 key 抄过去（`body->accsKind == GCAccsKind::StructKey ? addAccsNode(type, body->key()) :
  addAccsNode(type, body->value)`，draft 两种写法都有，`jvp.cpp` 的 `Accs` 一格就是这么写的）。代价：约两行 +
  一条用例；**风险未量**：切线端的同一格也读 struct 的成员（`Tangent<Point>` 同样是 struct），抄过去之后要
  验证后半的残差投影、`transpose` 一侧对 keyed 读的处理，以及 `lin` 的闭包布局。
- **丙**：在 `valuePortsOf`/`tangentArgumentsOf` 那一层拒掉 struct 端口。代价：**今天能切的「struct 原样穿过」
  也一起拒掉**（上表第一行），严格比甲差，不取。

## 默认建议

**甲留下，乙单开一条车道**，先红后绿：现成的 `split_struct_port`（绿）是「端口本身要能切」的守卫，
`split_struct_field_read`（现在是拒绝）就是乙的靶子 —— 把它改成期望
`forward y= 11.0` / `lin dp= 4.0` 即可。乙做成之前，甲那句拒绝是这一形状的正确答案。