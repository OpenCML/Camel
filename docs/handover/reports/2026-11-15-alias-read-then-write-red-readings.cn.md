# 「别名下先读后写」的红读数（2026-11-15，车道 `lane-aliasro`）

## 这份页为什么存在

作者 2026-11-15 上午下了一条返工令（「别名下"先读后写"排反了，结果确定但错」），
同日傍晚又落了 `e3447538c`「`var` 只是值的名字，语言里没有可变单元」，把那条返工**停下**：

> **停下**（未开工的不做，已落地的随下面第 1–3 项删除）：「别名下先读后写排反了」与
> 「`sync` 块外对 `var` 形参的写被丢掉」两条返工（**随第 1、2 项自然消失，用例保留作验收**）

所以这条车道没有提交任何实现或用例。但它在 **`0e94f3b` 的干净树上量出的红读数仍然有用**：
它证明那条返工所依据的缺陷**今天真实存在、四后端确定、且不是不稳定**。新排第 2 项
（`var` 形参降成多返回值、同根两个 `var` 实参是编译错误）的验收要重新写，而写它的人
需要知道旧形状当时读出什么。

**只留下读数和程序原文**是这份页的全部目的：只写读数不写输入，下一个人仍然跑不起来。

## 读数（`0e94f3b`，干净树，四后端各跑一次）

| 形状 | nvm | fvm | jit | prl | 应当 |
|---|---|---|---|---|---|
| 两个 `var` 实参同一变量 `g(x, x)` | `g 2 2` | `g 2 2` | `g 2 2` | `g 2 2` | `g 0 2` |
| `var` 实参与闭包捕获同一变量 `h2(y)` | `h 2 2` | `h 2 2` | `h 2 2` | `h 2 2` | `h 0 2` |
| 经 `if` 选函数值 | `if 2 2` | `if 2 2` | `if 2 2` | `if 2 2` | `if 0 2` |
| 经 `apply` 转手 | `apply 2 2` | `apply 2 2` | `apply 2 2` | `apply 2 2` | `apply 0 2` |
| 两者叠加 | `both 2 2` | `both 2 2` | `both 2 2` | `both 2 2` | `both 0 2` |

**读到的值是后面那次写造成的**：`t` 应当取到写之前的旧值 `0`，实际取到 `2`。
每一行第二个数（写之后变量的值）都是对的；错的是读那一下。

**回归对照（先写后读）四后端全部正确**：`g 1 1`、`h 1 1`、`if 1 1`、`apply 1 1`、`both 1 1`。
这个方向一直是对的，所以任何修法都不能让它动。

## 程序原文（两个用例，逐字）

`var_alias_read_then_write.cml`：

```camel
func g(var a: int, var b: int): int sync {
    let t = b;
    a = 2;
    return t;
}

func applyv(f: (var y: int) => int, var v: int): int sync {
    return f(v);
}

func main(): int sync {
    var x = 0;
    let t1 = g(x, x);
    println('g {} {}'->format<t1, x>);

    var y = 0;
    let h2 = (var p: int): int => sync {
        let t = y;
        p = 2;
        return t;
    };
    let t2 = h2(y);
    println('h {} {}'->format<t2, y>);

    var z = 0;
    let h3 = (var p: int): int => sync {
        let t = z;
        p = 2;
        return t;
    };
    let chosen = if true then h3 else h3;
    let t3 = chosen(z);
    println('if {} {}'->format<t3, z>);

    var w = 0;
    let h4 = (var p: int): int => sync {
        let t = w;
        p = 2;
        return t;
    };
    let t4 = applyv(h4, w);
    println('apply {} {}'->format<t4, w>);

    var u = 0;
    let h5 = (var p: int): int => sync {
        let t = u;
        p = 2;
        return t;
    };
    let t5 = applyv(if true then h5 else h5, u);
    println('both {} {}'->format<t5, u>);
    return 0;
}
```

`var_alias_write_then_read.cml`（回归对照，与上一个是同一组形状，只是写的顺序在前）：

```camel
func g(var a: int, var b: int): int sync {
    a = 1;
    let t = b;
    return t;
}

func applyv(f: (var y: int) => int, var v: int): int sync {
    return f(v);
}

func main(): int sync {
    var x = 0;
    let t1 = g(x, x);
    println('g {} {}'->format<t1, x>);

    var y = 0;
    let h2 = (var p: int): int => sync {
        p = 1;
        let t = y;
        return t;
    };
    let t2 = h2(y);
    println('h {} {}'->format<t2, y>);

    var z = 0;
    let h3 = (var p: int): int => sync {
        p = 1;
        let t = z;
        return t;
    };
    let chosen = if true then h3 else h3;
    let t3 = chosen(z);
    println('if {} {}'->format<t3, z>);

    var w = 0;
    let h4 = (var p: int): int => sync {
        p = 1;
        let t = w;
        return t;
    };
    let t4 = applyv(h4, w);
    println('apply {} {}'->format<t4, w>);

    var u = 0;
    let h5 = (var p: int): int => sync {
        p = 1;
        let t = u;
        return t;
    };
    let t5 = applyv(if true then h5 else h5, u);
    println('both {} {}'->format<t5, u>);
    return 0;
}
```

## 新排第 2 项接手时要注意的两件事

1. **`g(x, x)` 这一行在新语义下不是「打印 `0 2`」，而是编译错误**（「同一次调用里两个 `var`
   实参以同一个名字开头，是编译错误（重复的写回目标；下标运行期才知道，所以按根判）」）。
   所以上面那张表**不能直接当新用例的期望值**：前两行要变成「编译被拒」，后三行（经函数值、
   经 `apply`）才会回到「按书写顺序给出 `0 2`」，而且写的顺序要靠写回机制保证，不是靠别名边。
2. **回归对照 `var_alias_write_then_read.cml` 的第一行同样会变成编译错误**，其余三行保留。

## 量这条读数时踩到的一件事

车道用了 `git stash`（我派工时没有禁——后来才补的禁令）。`git stash` 是**仓库级**的栈，
跨工作树共享：它在同一小时里与 `lane-varexit` 的条目互相弹了一次，两边的工作树一度互相
被写进对方的内容。谁都没有损失（`varexit` 的那条被 `git stash store` 放回栈顶，另一条
74 文件、1985 行的 `wzj-dev: wip-static-branches` 始终在栈底、未被动过），但**结论是：
车道不许用 `git stash`，要备份就 `git diff > <文件>.patch`**。