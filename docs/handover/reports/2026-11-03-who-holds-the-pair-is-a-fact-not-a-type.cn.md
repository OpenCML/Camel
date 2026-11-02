# 谁手里"整"就是 pair，要按事实记，不能按类型判

**2026-11-03，`lane-loopreverse`。一条踩过的坑，写在这里是为了下一次不用再踩：两状态循环的值
`Tuple(pair, pair)` 和整值的 pair **同型**，所以"这个节点的类型等于 pair 的类型"不是"它手里有
pair"的证据。**

## 坑长什么样

`jvp` 的出口要判断：链尾那个节点的值**已经**是 `(value, tangent)`，还是"value 和 tangent 两个
半边"要拼成一个 pair。第一版写法是拿类型比：

```cpp
// 改前：类型相等就当作"已经是 pair"
const bool alreadyLifted =
    chain != nullptr && exitPair != nullptr && chain->dataType == exitPair;
```

对**一状态**的循环这是对的，而且对得干脆：`jvp` 给 LOOP 节点的值类型就是那个状态的 pair，出口
拿到的就是它。对**两状态**的循环，`jvp` 写的值类型是 `Tuple(pair(s0), pair(s1))`；而被提升的
函数出口要的 pair 是 `Tuple(Tuple(s0, s1), Tuple(s0, s1))`。两者**是不是同一个类型**，取决于
`s0`、`s1` 是什么：

- `s0 = s1 = float`（最常见的一种，`loop a = x, b = 0.0 …`）：两边都是
  `((float, float), (float, float))` —— **完全同型**。
- `s0 ≠ s1`（比如一个 `int` 状态加一个 `float` 状态）：两边不同型，这行判断碰巧不出错。

所以这不是"边界情况"：**最容易碰上的那种循环，恰好是同型的那一种**。类型说"已经是 pair"，
而节点手里其实是"每个状态一对"，出口于是把两对当成 value 和 tangent 端出去 —— 内容错、类型
对，跑起来不报错。

## 那一笔里怎么修的

改成一个**逐节点的事实**，和类型无关：谁手里是整的 pair，就把谁记下来。

```cpp
// The nodes whose *copy* is the pair itself: a lifted call, and a loop with one state and
// nothing collected (its value is that state, so the pair is the value). Written down
// because a type cannot say it: with two states a loop's value is `Tuple(pair, pair)`,
// which is the same type as the pair of the whole value, and only this set tells the exit
// which of the two it is holding.
std::unordered_set<gc_node_ref_t> pairIsTheCopy;
```

加入这个集合的，是**确实产出 pair 的那几种节点**，各有各的理由：

| 节点 | 为什么它的 copy 就是 pair |
| --- | --- |
| 被提升的 CALL | `reinterp<jvp>(f)` 的返回值就是 `(value, tangent)` |
| 分支的 JOIN | 每条臂往 Join 里写的就是 pair，Join 的类型因此是 pair |
| 单状态、不收集的 LOOP | 运行时的值**就是**那个状态，而 `jvp` 把它提升成了那个状态的 pair |

出口于是这样读：

```cpp
const bool alreadyLifted = chain != nullptr && exitPair != nullptr &&
                           chain->dataType == exitPair && pairIsTheCopy.contains(oldValue);
const gc_node_ref_t exitValue =
    !alreadyLifted && valueOf[oldValue] != rt::kInvalidNodeRef ? valueOf[oldValue] : chainValue;
const gc_node_ref_t out =
    alreadyLifted ? chainValue : liftedPair(valueTypeOf(oldValue), exitValue, chainTangent);
```

`contains` 不是装饰：类型相等只说明"看起来对"，`contains` 才说明"这个节点手里确实是 pair"。
两个方向都可能错，而只有这一句能区分它们。

## 这一条是怎么被发现的（值得记的部分）

修的时候以为只要改循环那一处，结果 `feat/transforms/split` 的 `split_branch_arms` /
`split_branch_author`（本来绿的）立刻转红，报的是 `REINTERP jvp function=leaky skip=tangent`：
**一个没有循环、只有分支的函数**。原因就是上面这行判断被写成了"只认循环和调用"——Join 不在
集合里，于是它的 copy（本来是 pair）被当成"半边"再拼了一次，`tangentOf` 找不到而拒绝。

一句话：**这条坑有两个方向**。忘记把某个"整的"节点记进去，那个节点就会被拆开（拒绝）；把某个
"半边的"节点记进去，它就会被端出去当 pair（错数）。所以每加一种能产出 pair 的节点，都要在同一
笔里把它记进 `pairIsTheCopy`，并且问一句：**它的类型和整值 pair 同型吗？** 同型就必须记。

## 判据（以后可以用它检查）

1. 这个节点产出的是 `(value, tangent)`，还是 value 或 tangent 之一？—— 是前者才进集合。
2. 它的类型和 `pairOf(整值)` 同型吗？—— 同型**而它不是 pair** 的节点，绝对不能进集合；
   反过来，只要它确实是 pair 就必须进，哪怕类型看不出来。
3. 数字：两状态循环 `loop a = x, b = 0.0 for k in [2.0, 3.0]` 在 `x = 1` 处的 primal 与
   tangent 都是 `13.0`。这一条要**同时看两行**：`lift v=` 和 `lift d=`。类型判据判错的方向是
   "把每个状态一对当成 value 和 tangent 端出去"—— 类型对、内容错，**跑起来不报错**，只有这两个
   数字会变；而"该记的没记"的方向会直接拒绝（本笔里 `leaky` 的 `skip=tangent`）。
   所以验收永远是两行数字，不是"能不能跑"。