# A `sync` body whose last statement is a write is printed as three statements

**状态**：待修（未修）。打印器侧。2026-11-02 连续五次尝试未落地，交接。

## 一句话

`sync` 体的值是它最后一次写的值。打印器写不出来这一句，于是把一句拆成三句；重编译后退出链上的
两个闸门变成一个。

## 最小复现

```cml
func main(): int sync {
    var t: int = 0
    let step = (y: int): int => sync {
        t = t + y
    }
    step(1)
    println(t)
    return 0
}
```

```
camel <此文件> std::cml            # 打印
camel <此文件> std::jgir canonical=true > a.json
camel <打印结果> std::jgir canonical=true > b.json
python3 test/tools/cml_roundtrip.py a.json b.json
```

打印结果：

```cml
func main(): int sync {
    var t: int = 0
    let step = (y: int): int => sync {
        let cml1: int = t + y
        t = cml1
        return cml1
    }
    step(1)
    println(t)
    return 0
}
```

读数：回程 `DIFFERENT`；`g2` **7 个节点对 6 个**；结构差报的是 `g1:n8`，一个带两个 normal 输入的
`:var/after`。答案两侧都是 `1`——**结构差不是答案差**。

`consistency/values/closure_var` 是同一件事的完整形态：`g2` 12 对 11、`g3` 8 对 7，答案 `3 / 24 / 249`
两侧相同。

## 病根（有读数）

`CmlPrinter::blockValueWrite` 是"区域的值 = 它最后一次写的值"的入口。它的 `handsTail` 判据要求
`tail` 是**退出节点的直接 normal 输入**。但真正交给打印器的 `tail` 在链的**下面**：

```
[cml-probe2] region=0 tail=4 exit=7 sync=1 write=-1     (step 的体)
[cml-probe2] region=0 tail=6 exit=11 sync=1 write=-1    (add 的体)
```

`exit=7` 的直接输入是 `n6`（承载值的那个 `sync` 闸门），而 `tail=4` 是 `n4`（值节点本身）。判据不成立
⇒ `write=-1` ⇒ **认写这一步从来没跑过**。

退出链的形状（源图，`closure_var` 的 `g3`）：

```
n5 :op/assn_l
n6 GATE(normal<-n4, sync<-n5)
n7 GATE(normal<-n6, control<-n5)   <- exit
```

打印成三句后重编译得到 **一个** `GATE(normal + control + sync)`，所以少一个闸门。

## 试过并失败的（不要重复）

1. **只改判据为"`tail` 站在链上"**：`write` 仍是 `-1`——比较的是链上的闸门，而 `tail` 是链**底**的值节点。
2. **放开 `regionNeedsSync(0)` 守卫**（`sync` 体也认写）：打印文本一字未变，但 `test/cases/trans/cml/var_param.cml`
   的契约用例**坏了**（`feat.trans.cml` 报 `AssertionError: var_param.before.json / after.json`）。
   ⇒ **那条守卫是有承重的**，不能只是去掉。
3. **把退出链从 `valueUsers` 里排除**（让值节点只剩写这一个使用者）：文本变成
   `total = total + y + y` + `return total + y + y`，值被重复求值，`g3` 反而 10 对 8。
4. **`assignmentHandingOn` 穿过闸门找写的值**：单独改无效（入口没进），与 3 同用时把 `var_param` 打坏。

## 下一班可以怎么接

- 先回答一个语义问题：**`sync` 体的值"就是它最后一次写的值"吗？** 若建图侧并不这么认为（`g3` 的值节点
  `n4` 同时被写和退出闸门读，正是"两个使用者"），那打印器把一句拆成三句是**忠实**的，要改的是**建图**
  或**判据的定义**，不是打印器。
- 若认定打印器侧：入口判据要以"`tail` 是退出链上任意一点（含链底的值节点）"为准，**且**必须让
  `regionNeedsSync(0)` 的承重之处另找办法——第 2 条读数说明直接去掉它不行。
- 判据：`g2` 12 对 12、`g3` 8 对 8、`closure_var` `EQUAL`、`var_param` 契约用例仍过、
  `feat.trans.cml` 与 `doc_readings` 全绿，且 `printed 559 / checked 552 / equivalent 511 /
  not_equivalent 41 / unfaithful 7 / DIFFERENT 48` 六项不回退。

## 为什么不把它登记进语料

它是 `DIFFERENT` 的病例：登记进 `test/cases/trans/cml/` 会被全量 sweep 计入，`not_equivalent` 41→42、
`DIFFERENT` 48→49，读起来像退步。所以最小复现只放在本页与人工命令里；修好之后再登记成正面用例。

## 相关

- `docs/design/graph-store/plan-roundtrip.cn.md` 的第二十二、二十三刀两节（查证与回退记录）。
- `CmlPrinter::blockValueWrite`、`CmlPrinter::assignmentHandingOn`、`CmlPrinter::valueUsers`、
  `CmlPrinter::regionValueWrite`。