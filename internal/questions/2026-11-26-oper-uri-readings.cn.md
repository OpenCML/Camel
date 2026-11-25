# 问：算子「按编号认」还差两件事，缺哪一件由你定

写给负责人。两条车道（`oper-leftover`、`oper-uri-to-record`）各自量到同一件事的不同半边，
合起来是同一个问句。**下列数字都是车道在 `1b7b986117` 干净树上量出来的，不是转抄。**

## 一、事实

`src/kernel/include/camel/core/operator_table.h:25-26` 写的是：

> Passes and back ends recognise an operator by its OperId, never by comparing URI text (design 3.8, R10)

车道按「算子按编号认、不按 URI 的字母认」这一条定义，在 `src/` 里量到 **56 处**仍在比较 URI 文本，
其中只有 **4 处**今天就能改（编号已存在、从 payload 或已算出的 `id` 取得）：

| 文件:行 | 今天读 | 应读 |
|---|---|---|
| `kernel/runtime/loop.cpp:510` | `GCOperBody->uri() != ":struct/append_arr_owned"` | `->oper != oper::AppendOwned` |
| `std/cpp/function_emitter.cpp:2241` | `uri == ":op/assn_owned"` | `id == oper::AssnOwned` |
| `std/cpp/function_emitter.cpp:2344` | `operatorUri(...) != ":var/take"` | `operatorId(...) != oper::VarTake` |
| `std/cpp/function_emitter.cpp:2385` | 同上（`args.front()`） | 同上 |

**其余 52 处不是「名字过时」，而是缺东西**：

- **7 处**：编译期声明记录里没有 `OperId` —— `OperatorIndex` 只有 `name_ / type_ / uri_`
  （`src/kernel/include/camel/core/operator.h:63-76`）。要走 payload 就得再加一个「问任何节点的 id」的读者，
  而判据③（读法只减不增）不许。
- **14 处**：算子表里没有这些算子的编译期 id。调用点 `intern` 会撞「well-known 先驻留、顺序固定」的契约
  ⇒ 静态初始化就可能挪 id。
- **31 处**：缺一条记录事实（成员读 9 / 「交给它的数组会长大」9 / 引用律 11 / `where` selector 1 / shape-metadata 1）。

另有一条硬事实：`operUriOf` **删不掉**（21 处调用仍在）。**只改那 4 处，不会让任何读法消失。**

## 二、问句

**（i）编译期声明记录要不要带上 `OperId`？** 带上之后那 7 处可以改；不带，那 7 处只能继续比 URI。
它牵动的是「`:op/…` 归 core 还是归模块」。

**（ii）well-known 算子 id 列表要不要扩？** 要扩的就是这 14 个：
`:op/copy`、`:op/fill`、`:op/access`、`:op/access_key`、`:op/cast`、`:op/enter`、`:struct/range`、
`:op/slice_sub`、`:op/slice_str`、`:op/slice_bytes`、`:op/idx*`、`:op/fail`…
代价是**顺序固定**这条契约要重新说明：静态初始化时谁先驻留。

**（iii）那 31 处「缺一条记录事实」各自进表，还是先挑一簇做样？**

## 三、我的默认做法（不等回复也照这个开工）

```
① 4 处现在就改，一类一提交，判据是「改前/改后同一用例读数不变」
② (i)(ii) 无裁决前，52 处不动；不为改这 4 处新加任何读者
③ 每次动之前先量 `operUriOf` / `operatorOf` / `GCOperBody` 的行数，记录数不减就不算进步
```

## 四、这件事为什么值得你花时间

R10 是设计原则，而 `src/` 里仍有 56 处按文本认算子。**它们是「原则已写下、代码还没跟上」的存量**，
不是某一条车道的活。今天能做的只有 4 处 —— 这个比例本身说明：**要么原则需要一条迁移路径，要么它需要被重新说明。**