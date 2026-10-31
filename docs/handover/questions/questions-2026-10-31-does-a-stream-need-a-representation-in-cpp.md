# 流要不要在 `std::cpp` 里有一套表示

> **流的值本来就是一个步进函数加它的状态（`include/camel/core/type/composite/stream.h:30-58`）——所以问题不是"能不能表示"，而是"用哪种表示，谁来定"。**

日期：2026-10-31　车道：`lane-nestedlit`（`std::cpp` 写出器）　测量树：`b3bb51280`（`origin/develop`，已合）+ 本车道三笔

**要作者答的一句**：`std::cpp` 给流**一套表示**（`rt::Stream<T>`，句柄到步进闭包加状态），
还是**不表示**、改由一个遍把生成器消解掉（`yield` 体降成循环/回调）？两条路的代价各一句，按字面执行即可。

## 现象

**`std::cpp` 今天一个流类型都不写。** 证据三处：

1. 运行时里没有流：`include/camel/standalone/` 十九个头（`array.h`、`fn.h`、`record.h`、`opt.h`…）
   没有 `stream.h`；`src/passes/trans/cpp/cpp_types.h:50-52` 那句"`std::cpp` 翻译什么"的清单里也没有流：
   ```cpp
   "std::cpp translates numbers (real and complex), bool, string, enums, arrays, tuples, structs, T?, T!, "
   "functions and var variables of these, and void"
   ```
2. 类型映射里没有 `Stream` 这一格：`src/passes/trans/cpp/cpp_types.cpp:400-437` 的 `switch (t->code())`
   从 `Int32` 走到 `Never`，`default: break;` 落到注册表再返回 `std::nullopt`。
3. 于是任何签名带流的地方在写函数头时就被拒（`src/passes/trans/cpp/function_emitter.cpp:197-209`，
   `add(port, "a parameter")` / `"returns a value"` → `program_.values.type(type)` 为空 → `refuse`）。

两条原话（同一句话，两个位置）：

```
a parameter of function 'counted' has type 'int..'; std::cpp translates numbers (real and complex), bool,
string, enums, arrays, tuples, structs, T?, T!, functions and var variables of these, and void.
function 'count' returns a value of type 'int..'; std::cpp translates numbers (real and complex), bool, ...
```

**为什么这不是"少一条规则"**：流的值就是它的步进函数加状态——`StreamType::stepType()` 给的是
`() => (T, T..)?`（一个元素加剩下的流，或者 `null` 说到头了）。这个类型**含它自己**，
所以 C++ 里写不成别名（`using S = std::function<std::optional<std::tuple<int, S>>()>` 不成立），
必须落到一个命名的结构体加一层间接。那是**表示**的决定，不是类型表里补一行。

**这一族的分布**（全语料逐程序扫，只数"第一个拒绝句里点了流类型"的，共 **35** 条）：

| 位置 | 条数 | 说明 |
|---|---|---|
| `function ... returns a value of type 'T..'` | 22 | 生成器函数的**返回类型**（`yield` 体） |
| `a parameter of function ... has type 'T..'` | 13 | 消费者/传递函数的**参数类型** |

类型：`int..` 27、`string..` 3、`(int, int)..` 2、`(int, int..)` 1、`(int.., int)` 1、`int!..` 1。
目录：`test/cases/compile/stream/` 一族 34 条，`test/cases/modules/stream/library.cml` 1 条。
解释器侧都跑得通（`for_loop.cml` 印 `6 0 3 3 6`，`generators.cml` 印 `56 1234 10 -10 5`）。

## 两条路的代价

**(A) 给流一套表示（`rt::Stream<T>`）。** 照 `stream.h` 的话写：值是步进闭包加状态，句柄一层间接解决自引用；
代价是一个**新运行时类型**加它的拉取写法（`for x in s`、`tee` 的两路消费者、线性——一个流只能消费一次），
而且这层表示一旦进 cpp，"流的元素从哪来"就两次实现了。

**(B) 不表示流，改由遍把生成器消解掉。** `std::cpp` 继续只写元素与结构体；
代价是只有"生成器与消费者在同一份图里、消费者已知"时才降得掉——返回流（22 条）、
把流当参数往下传（13 条）、流放进 `var` 或容器、`tee` 的两路、拉取顺序由调用点决定，都降不掉，仍然拒。

## 我方默认建议

**不裁**（作者口径：答不出来就问，不定 ad hoc）。若一定要一句倾向：**A** —— 流的语义已经定死
（值是步进函数加状态），表示它不引入新语义，只给它一个名字；B 的降级在 35 条里有 35 条都不是
"消费者就在旁边"的形状。这条倾向不作为落地依据。

## 边界：作者第 11 条那三句不动

任何表示都必须照这三句（`docs/handover/reviews/questions-2026-10-29.md`，第 11 条），
本页一个字都不改：

- **错误值进单元**：错误值是普通的值，进 `var` 的单元与别的值一样，单元不区分错误与非错误，状态结构体不为错误另设字段。
- **循环携带量做成单元**：跨 `yield` 存活的循环携带量都是单元（按句柄进状态结构体），`pc` 指向循环体里那个 `yield`，下一次拉取从那里接着走，不另造"循环状态"。
- **`return` 表示结束流**：生成器里的 `return` 结束流；已经产出的元素有效，下一次拉取得到结束；`T..` 的生成器不许带值的 `return`。
- （第四句同样不动）**可错元素**：元素类型是 `T!` 时，错误元素也是一个元素，消费者拿到后还能接着拉；体里出的故障结束那一次拉取，流到此为止。

## 数字口径

本页的 35 条与 `refused 1005 / differs 0 / defect 54` 都是**这一棵树**的读数（HEAD `b3bb51280` + 本车道三笔，
用 `test/tools/cppexec/census.sh` 与逐程序扫描测得）；另一棵树上报的 `971 / 0` 不是同一次测量，
**不并称"今夜的数"**。`defect 54` 里绝大多数是 census 自己那条编译命令的链接失败（`para/*`、
`trans/cpp/runtime_bridge.cml`——后者在 `expect.txt` 里、闸门是全绿的），不是写出器的缺口。