# 规则的条件下改动了正在改写的程序

## 问题

改写的检查提交（`RuntimeGraphDraftSession::commitChecked`，见
[docs/technical/17_graph_rewrite_api_contracts.md](../../technical/17_graph_rewrite_api_contracts.md)）会在
"会话读过或写过的图，在上下文现在的程序里已是另一个版本"时抛 `GraphCommitConflict`。要走到这一步，必须有**另一个提交**
在改写会话开着的时候替换了上下文的程序。让规则自己的条件去做这件事，是用户能写出来的：

```camel
// 见证 B：条件下的图事务提交，把正在被改写的程序换掉
rule add_zero(x: int) when publish_now() { x + 0 => x }

func publish_now(): bool {
    let p = current()
    match do_publish(p) {
        case error e => sync { println("inner error value: ", e); return false }
        case q => sync { return true }
    }
}

func do_publish(p: Program): Program! sync {   // 同步函数，副作用才会执行
    let t = open(p)
    let n = add_constant(t, root(p), 1)
    println("added node ", id(n))
    return commit(t)
}
```

在这次改动之前，它报的是 `Pass 'std::default' failed with an internal error: The commit conflicts with
a newer program: 1 graph(s) the session used have another version there ('__root__')`——把用户自己写的规则说成编译器
缺陷。现在改写模块把这一异常转成错误值 `RewriteConflict`（`modules/rewrite/module.cpp`，三个内核），
`@fails` 与生成页也随之写明。

## 要裁定的：条件下的**嵌套改写**让编译器崩溃

同一种"条件下提交"，走 `rewrite::rewrite` 而不是图事务时，编译器直接死掉：

```camel
// 见证 A：条件下的嵌套改写
rule add_zero(x: int) when publish_now() { x + 0 => x }
rule drop_one(x: int) when true { x * 1 => x }

func publish_now(): bool {
    let p = current()
    match rewrite(p, [drop_one]) {        // 嵌套改写：自己开一个会话并提交
        case error e => sync { println("inner error value: ", e); return false }
        case q => sync { return true }
    }
}

func redex(x: int): int { return x * 1 + 0 }

func main(): int sync {
    let p = current()
    match rewrite(p, [add_zero]) { ... }
}
```

读数（Linux 主机，`camel <case>`）：**退出码 null（被信号杀死），stdout 与 stderr 都为空**，一秒左右。
可复现（同一程序跑两次同样如此）。与见证 B 的差别只在"用什么提交"：嵌套改写让**外层会话的基准程序**在整个改写期间被换掉，
而外层会话的草稿、`committedNodeRefs` 还按旧程序读数——像是旧程序/旧图被回收后仍在读。

## 选项与代价

1. **单开一条缺陷车道**：按 GC 毒化复现（`CAMEL_GC_POISON=1`）找到"旧程序被换掉后仍被读"的那一处并修掉；验收用例就是
   见证 A 的程序，断言它报 `RewriteConflict` 而不是崩溃。代价：要读 draft 会话与程序发布的回收边界，改动面在
   `src/runtime/draft_session.cpp` 与改写模块的根作用域，风险中等。
2. **先禁掉这条路**：让改写会话在开着的时候拒绝被替换（或让条件下的提交本身成为一条诊断）。代价：要定一条新语义
   （"条件不得提交"），属于设计原则的范围，需要负责人拍板；好处是把崩溃变成一条准确的消息。
3. **只记账不修**：把它列进已知缺陷，本车道不动。代价：用户的一条规则就能让编译器崩溃，且没有诊断。

倾向：**选项 1**，并且在修好之前本车道新加的 `RewriteConflict` 只覆盖"提交撞版本"这一条（见证 B），
不假装覆盖崩溃。