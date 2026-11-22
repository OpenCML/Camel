# 状态元组里装不下任何程序局部——在 SSA 之前

> 这是一页**读数**，不是一页待裁。写给明天问「为什么 `capture_ports_closure` 一直不动」的人。

## 读数

在 `bc5ce2249`（步进把状态解包成自己的局部）之上，给 `visitGeneratorBody` 加一行临时打印，跑
`test/cases/compile/stream/generators.cml` 的四个生成器：

```
[GENTYPESRC] step=__gen_step__ slots=[] cells=[__gen_pc__ __gen_halt__ i ]
[GENTYPESRC] step=__gen_step__ slots=[] cells=[__gen_pc__ __gen_halt__ ]
[GENTYPESRC] step=__gen_step__ slots=[] cells=[__gen_pc__ __gen_halt__ ]
[GENTYPESRC] step=__gen_step__ slots=[] cells=[__gen_pc__ __gen_halt__ x read ]
```

`slots` 是「能进状态元组的局部」，`cells` 是「留下来当格子的名字」。**四个生成器全部 `slots=[]`。**

## 原因

生成器体的每个局部都是 `addCellOfType` 造的 `:var/new/empty` 单元格，`value_type` 是 `var int`
这类**格子类型**（GCT 里 `var T`）。`zeroValueOf`（`src/compile/gct/builder.cpp`）对 `var T`
返回 `nullptr`——它归在 `Ref` 那一支的注释里——所以**一个程序局部都进不了槽**。

于是 `S = (pc, halt)`，而 `stateNow()` 打包的也只有这两项。**状态按值传递这件事本身是对的、
已经在跑；它只是没有东西可装。**

## 结论

**SSA 之前，生成器的状态元组装不下任何程序局部；因此 `capture_ports_closure` 不可能因为
「状态按值」而下降。** `83` 这个数归计划页的 S6（「B 的只读程序捕获按值」），不归生成器这条线。

为什么不把 `var T` 拆成 `T`、只让「下文不再写」的局部进槽：那会造出**第三种做法**——格子、按值、
「没写过所以算值」——而作者的计划只允许在声明处一次判定、只有两种做法。**能跑通但不该存在的状态，
比跑不通更贵。**

## 两条被推翻的判断（留档，省下一个人一天）

1. **「下降要等 `readCell` / 解包位置改对」**——不是。解包位置已经在 `fill` 之前，`147/147` 也绿，
   但槽是空的，所以解包从来没解出过 `x`；`__g_71__` 捕的自始至终是外层那个格子。
2. **「`captureByValueOf_` 的 origin 发重了，两个值共用一个 origin」**——**没有证据**。那是推测，
   不是读数：槽都没造出来，就没有第二个 origin 可谈。**推测写进报告必须标「推测」**，否则下一个人
   会把它当读数用（这一条正是这么发生的）。

## 一条判据

**在 S6 之前，`capture_ports_closure` 的任何下降都应被怀疑**：一个不会动的数本身就是一条判据，
它动了就说明有别的东西在改它。