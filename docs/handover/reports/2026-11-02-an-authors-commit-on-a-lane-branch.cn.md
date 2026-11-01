# 作者的提交挂在车道分支上：`9048c30ea` 是怎么找回来的

> 2026-11-02。这一页记一件事和一条规矩。它进这一页，是因为**"作者自己的提交跑到车道分支上"是最容易整笔丢掉的东西**：它不影响任何人的门，也不出现在 `develop` 的任何红灯里，只在下一次有人要它的时候才发现"找不到"。

## 事实

`fsh-dev` 上没有这一笔：

```
$ git branch -a --contains 9048c30ea
+ lane-lit2
$ git rev-list --left-right --count 9048c30ea...origin/fsh-dev
3	55
$ git show -s --format='%p %an %ad %s' 9048c30ea
65d8dbb85f0e5c757717e885e8a2dd11c26b705a  Zhenjie Wei  2026-11-02 04:02:10 +0800  vjp is one declaration for every arity
$ git merge-base --is-ancestor 65d8dbb85 origin/develop ; echo $?   # 1
$ git merge-base --is-ancestor 65d8dbb85 origin/fsh-dev ; echo $?   # 1
```

- 它是作者 04:02 的一笔：`stdlib/vjp.cml` 42 行、`test/cases/compile/transforms/vjp_arities.cml` 42 行、`test/plans/feat/compile/transforms.plan.toml` 44 行——**对所有元数只有一条 `vjp` 声明**，就是 19:10 那条裁定的落地。
- **它不是独立的一笔**：它的父提交 `65d8dbb85`（"The tuple a lambda's rest parameter stands for is the shape the spread reads"）与祖父 `1cb1299e2`（"A lambda's rest parameter is a value in its body"）**既不在 `develop` 也不在 `fsh-dev`**，只在 `lane-lit2` 上。所以"只并 `9048c30ea`"做不到：它站在两笔未合并的实现上，那两笔改的是 `src/compile/gir/builder.cpp|h`（lambda 形参表里的 `..xs` 成为体内的一个值、它代表的元组就是 `..xs` 展开读到的形状）。
- 三笔一起并（`--no-ff 9048c30ea`，把它的祖先一并带进来），闸门全绿。

## 并进来之后翻出来的三件事（都在同一轮的提交里）

1. **读数要跟着改**：`docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md` 里 `vjp.cml` 的行数是 **104**，这一笔之后是 **98**（三条按元数编的声明并成一条）。页里加了一句"为什么这个数动了"，并写明那三笔只挂在 `lane-lit2` 上、是本轮找回来的。
2. **生成文档要重生成**：`docs/stdlib-reference.md`（14 行表格从"每个元数一条"变成一条）与 `docs/reference/camel-for-ai-stdlib-signatures.md` 的 `stdlib-signatures` 区（同 525 条，内容变）。
3. **作者自己的文档例子掉了**：`stdlib/vjp.cml#vjp` 变成泛型后，它 `@example` 里的 `f` 没人能造出来，例子以 `UnresolvedReference 'f'` 归进 `scripts/stdlib-reference.mjs` 的 `EXAMPLE_STOPS['a semantic error']`，**39 → 40**。按那张表自己的规矩（"唯一的过法是在这里改数字，并在一笔里说清它为什么动"），数字改了、理由写在旁边，**不是开脱**：这是作者那一笔留下的，应该由把 `vjp` 泛型化的那条线把例子重新绑一次。

## 规矩

**并任何一笔作者署名（或任何"可能是他自己的"）的提交之前，先跑这三条**：

```
git branch -a --contains <sha>                       # 它到底在哪几条分支上
git show -s --format='%P' <sha>                      # 它的父是谁
git merge-base --is-ancestor <父> origin/develop     # 那个父在不在 develop 上
```

**父不在 `develop` 上 ⇒ 这一笔不是独立的一笔**，单独 cherry-pick 会让它悬在缺失的实现上；要么把那条链一起并（并在合并信息里说清带了哪几笔），要么先问。

同一天的另一半：作者的**交接物**（`docs/handover/reports/eval-defects-2026-11-01/`，README + 11 个最小程序 + 6 个缩减种子）在 `fsh-dev` 上悬了很久才进 `develop`，而那期间 A6 正卡在"找不到最小程序"。**两件事同一个形状：东西在，只是不在 `develop` 上。** 所以并完之后要顺手量一句：`git ls-tree -r origin/fsh-dev -- <目录>` 与 `origin/develop` 比，差多少。