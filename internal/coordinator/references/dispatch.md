# 派工模板（子代理读这一页，不要凭记忆）

**这一页是「派工提示」的正文。协调者派工时只给三样：基线 sha、要判的那一件事、指向本页的哪一节。**
理由见 `SKILL.md` §9：手写派工提示一条几千 token，于是「填满 8 格」变成 token 问题，而不是能力问题。

---

## 1. 通用纪律（每条车道都适用）

```
· 你只改自己工作树里的文件；**不许 push、不许 merge、不许碰 develop 与 main**。
· **路径一律绝对**（`D:\Projects\Camel-wt\<lane>\...`），`edit`/`write` 工具也一样。
  判据不是「我确认过」，是两行输出：
    `git -C D:\Projects\Camel-wt\<lane> status --porcelain` 里能看到你的改动；
    `git -C D:\Projects\Camel status --porcelain` **为空**。
· **不许 `-Tier all` / `-Plan all` / 整棵树**；**不许在本机构建**（会蓝屏）。
  构建与测试一律走 `internal\tools\camel-remote.ps1`（见 §4）。
· **不许打 `known_fail`、不许改 `baseline.json`、不许改既有断言。**
  一个红要么修掉，要么报出来；**把它标成已知失败等于删掉判据**。
· **撞墙就停手报**，尤其当你要修的东西**不在授权文件里**时——那是结论，不是失败。
· `-AstOnly` / `-UpdateAstHashes` 传多个文件名时用 `-Command` + `&`，**不要用 `-File`**
  （`pwsh -File` 会把逗号列表折成一个带引号的串，主机回
  `--only selected none of the tree's 4202 .cml file(s); refusing to update/compare nothing`）。
```

## 2. 报告格式（**≤12 行，跑完报我、等点名**）

```
① <判据第 1 项：你要先答的那个问题，或基线读数>
② <改了什么，文件+函数逐条>
③ <改后读数>
④ 六计划 + `[SUMMARY]` + 加数（**逐项写，不要只给合计**）
⑤ 阳性对照（证明你没过度修改的那一格）
⑥ 提交 sha + `git diff --numstat`
⑦ 卡在哪（**含「这件事不存在」这类结论**）
```

**数字必须带四样**：哪个群体、哪个时刻、哪个来源、哪个前提。
**两个不同树上的 `[SUMMARY]` 永远不许相加。**
`skip`、`known`、`fail`、`pass`、`total` 分开写；`skip` 不等于 `pass`。

## 3. 红线清单（按当前主干状态挑，**不许全抄**）

```
**永远不许碰**：`src/runtime/branch_arms.cpp`、`src/passes/opt/inline/engine.cpp`、
  `src/compile/gir/builder_*.cpp`、`src/parse/`（除非这条车道就是语法）
**按车道状态逐条挑**：哪条车道在改什么文件，就从别人的清单里排除那一段。
  ⇒ 派工时必须写清「另一条车道正在改 X，你不要改 X 的那一段」，
    并给出自查命令：`git -C D:\Projects\Camel log --oneline origin/develop..lane-<别人>`
**只读地读作者的设计页**：`git -C D:\Projects\Camel show origin/fsh-dev:<path>`
  ⇒ **不许检出 `fsh-dev`、不许合它**（那是作者的单向分支，我们往里读，不往里写）
```

## 4. 主机命令（只这一种写法）

```powershell
# 起点（新车道）
git -C D:\Projects\Camel worktree add D:\Projects\Camel-wt\<lane> -b lane-<lane> origin/develop
Copy-Item D:\Projects\Camel\internal\tools\camel-gate.local.json D:\Projects\Camel-wt\<lane>\internal\tools\camel-gate.local.json

# 六计划：**必须分六次跑**，`[SUMMARY]` 只给总数
pwsh -NoProfile -File D:\Projects\Camel-wt\<lane>\internal\tools\camel-remote.ps1 `
  -Path D:\Projects\Camel-wt\<lane> -Plan compile -Jobs 8
# 其余五次把 -Plan 换成 linear / opt / parse / para / tools
```

**`-Plan` 与 `-AstOnly` 用正斜杠。** 一个计划的结果是它那一行 `[SUMMARY]`。
**`-Clean` 没有 `-Name` 参数。**

## 5. 判据分类（按这五类挑一种写）

| 类 | 形状 | 关键句 |
|---|---|---|
| **A 之前/之后** | 同一程序、同一命令，改前读一个数、改后读另一个 | 「改前 X、改后 Y，两个都要报」 |
| **B 问到了没** | 判据是**「这件事被问过」**，不是「它被拒了」 | **「问到了、然后通过」是正确结果** |
| **C 阴性/阳性对照** | 一个该通过的、一个该拒的，两个读数必须**不同** | 「若两者读数一样，你什么也没加」 |
| **D 复现优先** | 第一步是**确认那件事真的存在**，复现不出就**撤回这一格** | **「复现不出比修好更值钱」** |
| **E 位置不变** | 结构改动的判据是**位置/顺序没动**，不是「节点少了」 | 「节点消失**且**位置不变」 |

**D 类必须写在第一句。** 今晚四次靠它省了整格：`visib` 推翻我的假设、`optord` 推翻 (B) 的前提、
`armread` 推翻我的算术、`outl` 把「中大活」判成「小活」。

## 6. 派工正文（照抄，只填 `<...>`）

> 你是 Camel 项目的实现车道。**先读 `camel-dev-ops` skill 的 `references/dispatch.md`**，
> 按 §1（通用纪律）、§2（报告格式）、§3（红线）、§4（主机命令）、§5（判据分类）执行，
> 不要凭记忆。
>
> **基线**：`<sha7>`（`origin/develop`）｜**工作树**：`D:\Projects\Camel-wt\<lane>`，分支 `lane-<lane>`
>
> **这件事**：`<一句话>`
> **判据**：§5 的 **`<类>`** 类 —— `<一条可测的读数>`
> **要答的问题**：`<若第一步是「确认它真的存在」，写在这里；否则删掉本行>`
> **不许碰**：`<按 §3 挑，列出具体文件；并写清哪条车道正在改哪一段>`
>
> 跑完报我、等点名。

## 7. 这一页怎么维护

**某条车道踩了一个新坑，就把那一条加进对应小节**——下一次派工自动带上。
已被加进来的（每条都付过代价）：

- 模板本身（§9 的由来）：手写提示 ⇒ token 决定车道数。
- §1 的 `-File` 逗号坑：`-AstOnly 'a','b'` 静默一个也不比，回一句 `refusing`。
- §2 的「两个 `[SUMMARY]` 不许相加」：我加过一次，得出一个不存在的合规数。
- §5 的 D 类：`armread` 那次我的判据自相矛盾（`2617+17=2634≠2633`），它报 **16** 才是对的。
- §5 的 B 类：判据写成「被拒了」会让一条正确的车道去修一个不存在的缺陷。