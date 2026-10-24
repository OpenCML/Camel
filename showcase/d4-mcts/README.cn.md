# D4 — 蒙特卡洛树搜索（井字棋）

一个真正的 MCTS：`--arg playouts=N` 次试走，每次从根走到可展开的结点、展开一个孩子、再用
浅策略把棋走完，把结果回传到根；最后由访问次数和均值选出着法并打印整棵树在根这一层的统计。

这个示例展示的是 **用值语义写一棵树**：`Node` 是结构体、孩子是 `Node[]`，`visit` 返回新
树而不是原地修改，所以同一组参数（种子 + 试走次数）在任何后端上都打印逐字节相同的输出，
"搜索对不对"可以写成断言，而不是"跑起来看着像"。

## 范围

H13 的 D4（`docs/design/showcase/README.cn.md`）写的是 **AlphaZero 简化版：MCTS + policy/value
网络 + self-play**。这里只做其中**确定的那一半**，并且做完整：

- 做了：UCT 树搜索、试走、浅策略（能赢就赢 → 挡对方 → 否则按生成器选空位）、四个后端可复现；
- 没做：policy/value 网络与 self-play。这是**范围（算力与篇幅）上的缩小，不是语言缺陷**：
  `modules/nn` + autodiff 已经能训练小网络，树搜索这一半可以直接复用，只是把示例做大到
  需要训练就不适合放进 showcase 的秒级计划里。搜索这一半也不需要"运行期数组变成张量"
  （H13 的 Q7 / H17 的 P2–P4），所以它不依赖那些还没落地的步骤。

## 目录

```
showcase/d4-mcts/
├── search.cml                Camel 程序（341 行，MCTS）
├── baseline/mcts.py          Python 对照（同样的搜索，标准库）
├── compare.py                跑两边、逐行比、量时间/内存、写 RESULTS.md
├── RESULTS.md                small 配置的实测结果
├── README.cn.md
└── README.md
```

## 搜索

局面是 `State = { board: int[], to_move: int }`：`board` 是长度 9 的数组，`0` 空、`1` 是 X、
`2` 是 O。八条获胜线写在 `LINES` 里。

- **试走**（`playout`）：`while` 局面没结束，就用 `policy_move` 走一步；走完返回
  `result_for`：从"根的行动方"看是赢 1.0、平 0.5、输 0.0。
- **浅策略**：先看自己有没有一步赢的线（`completing_move`），再看对手有没有（挡），都没有
  才用生成器 `draw % len(places)` 选一个空位。所以试走的质量足以让搜索找到一步赢和唯一的
  防守着法，而不会靠穷举。
- **UCT**：`score = 均值 + 1.4142135623730951 * sqrt(log(父访问数) / 访问数)`。孩子的
  `score` 按"走到这个孩子之后行动的那一方"记（也就是父的对手），回溯时翻号，所以根的行动方
  看到的永远是自己的期望。
- **展开顺序**：孩子严格按 `moves` 的顺序（空位从小到大）建，`len(children) < len(moves)`
  时展开下一个没建过的；都建过就按 UCT 下降。
- **选择着法**：根的孩子里均值最高者，并列取最小的 place（`best_move` 用严格 `>` 从前往后扫）。
- **确定性**：随机数来自计数器式的 Lehmer 生成器 `next_key(k) = (k * 48271) % 2147483647`，
  每个结点有自己的 key（孩子的 key 由父的推出），所以没有任何全局状态、没有时钟、
  没有哈希序，同一组参数必然是同一棵树。

数字上看：空棋盘 5000 次试走会建出 4929 个结点，根把 5000 次都算在 `root_visits` 里；中心的
均值最高（0.662403），搜索选它。

## 运行

```powershell
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/d4-mcts/search.cml --arg seed=42 --arg playouts=2000 --arg position=XX.OO.... std::nvm
camel showcase/d4-mcts/search.cml --arg seed=7 --arg playouts=5000 --arg position=......... std::jit
python showcase/d4-mcts/compare.py --profile small        # 两边都跑，逐行比
python showcase/d4-mcts/compare.py --profile standard     # 写 RESULTS.md
```

`position` 是 9 个字符：`X`、`O`、`.`（空），X 先走；`--arg to_move=` 默认由棋子数推出
（数目相等轮到 X）。程序只读这三个参数，不认识别的。

## 结果（small：2000 次试走，空棋盘 5000；种子 42）

Camel 与 Python 对照**逐行相同**（不是容差比较：树是值，两边算的是同一棵树）：

| 算例 | 局面 | best | 根的选中孩子均值 | Camel 秒 | Python 秒 |
|---|---|---|---|---|---|
| 现在就能赢 | `XX.OO....` | 2 | 1.000000 | 0.209 | 0.108 |
| 唯一的挡 | `.X..O.X.O` | 0 | 0.967624 | 0.315 | 0.220 |
| 双杀 | `X..OX...O` | 2 | 0.944802 | 0.438 | 0.151 |
| 空棋盘 | `.........` | 4 | 0.662403 | 1.606 | 0.494 |

"现在就能赢"那个孩子均值正好 1.0：走到那里局面就结束了，穿过它的每次试走都返回棋局结果。
双杀的位置只有 0.94 左右：树在双杀下面还会展开别的着法，那些试走的均值低于 1，这是
MCTS 的取值方式，不是算错（`test/plans/feat/showcase_d4.plan.toml` 里把 1.0 和"低于 1.0"
都写成了断言）。

四个后端（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`）在四个算例上输出逐字节相同；
完整表格在 `RESULTS.md`。

**没有拿时间当结论**：这里的秒数是整进程墙钟，只做参考。Python 反而更快（0.151 s vs
0.438 s，双杀局面），因为 Python 那边每个结点是一个 `__slots__` 对象、棋盘是 `list` 复制，
而 Camel 这边每次 `apply_move` 都按值复制 `State`（9 个 int 的数组 + 一个 int），并且要过
图的调度。这个示例要证明的是"同一个搜索在两边是同一棵树"，不是"谁快"。

## 并排看

| 事情 | Camel | Python |
|---|---|---|
| 局面 | `type State = { board: int[], to_move: int }` | `(board, to_move)` |
| 结点 | `type Node = { key, state, place, visits, score, moves, children }` | 同样的字段，`__slots__` |
| 孩子 | `children: Node[]`（递归结构体） | `node.children` 列表 |
| 走一步 | `var next = state; next.board[place] = …` | `played = list(board); played[place] = …` |
| 展开 | `grow(node, child)` 返回新结点 | `node.children.append(child)` |
| 试走 | `loop current = state, draw = key while … do { … }` | `while win == 0 and not full(...)` |
| 选着法 | `loop chosen = 0, index = 1 while … do { … }` | `for index in range(1, …)` |
| 生成器 | `(key * 48271) % 2147483647` | 同一个式子 |

用 Camel 的习惯写法：改树的四个辅助函数（`apply_move`、`record`、`grow`、`put_child`）把路径赋值
写在自己的同步函数体（`: T sync`）里，调用方按书写顺序执行即可（探针验证过）。`visit`/`search` 是纯的：
返回新树，没有副作用，所以四个后端跑出同一份输出。

## 遇到的问题

- **结构体字面量里的 `[]`**（本次唯一的语言缺陷）：当一个结构体字面量里同时有 `[]` 和另一个
  值是**变量**（类型是命名 struct）的字段时，`[]` 被推断成 `void[]`，整个字面量就不是声明的
  类型了：

  ```camel
  type State = { board: int[], to_move: int }
  type Node = { state: State, children: Node[] }

  func main(): int sync {
      let state: State = { board: [0, 0, 0, 0, 0, 0, 0, 0, 0], to_move: 1 }
      let node: Node = { state: state, children: [] }   // <-- 这一行
      println(node.state.to_move, len(node.children))
      return 0
  }
  ```

  ```
  [Error]: This value has type '{ …, children: void[] }', which is not the declared type 'Node'
      (name=SemanticDiag::…, on the line of the `Node` literal)
  ```

  二分出来的触发条件：同一个字面量里另有一个字段的值是**变量**，且它的类型是命名
  struct。全是常量或字面量就没事：`{ value: 4, children: [] }`、
  `{ value: 1, moves: [1, 2], children: [] }`、六个标量字段的结构体都能通过；把空数组
  放进一个带类型的局部量（`let none: Node[] = []`）再写进去也通过。示例里就是后者：
  `open_node` 先用 `let none: Node[] = []` 接住，再放进结点字面量——**这是一个绕法**，
  示例并没有绕开缺陷本身。

  评审里已经裁定：空数组字面量 `[]` 按上下文取类型（与字面量取类型同一规则，5.1），
  这一条不必等 H23。上面"只有字面量"的写法确实取到了 `Node[]`，而带变量兄弟字段的那个
  位置没取到并推成了 `void[]`，所以这是那条规则在这个位置上没落地，不是缺功能。真正的
  修法是让 `[]` 在结构体字面量里一律按声明的字段类型推断。
- **`return` 写在 `if`/`match` 分支里不算离开函数**（`SemanticDiag::ExitInUnusedBranch` 警告）：
  `visit` 三个分支各写一个 `return` 会有两个警告，改成一个
  `return if … then … else if … then … else …` 表达式就好了。
- **路径赋值要写在同步函数体里**，否则警告
  `The call of '<__assn__>: (var State, State) => State' is not in a sync block`。
  四个辅助函数把赋值写在 `: State sync` 的函数体里；同步函数体里的顺序不传染调用方
  （探针验证过），所以 `playout` 里调用它们不必再做什么。
- **`loop` 状态不能写空数组字面量**：`loop plays: int[] = [], …` 报
  `Expected '=' before ':'`（类型标注不支持），只写 `loop plays = []` 报 `LoopStateWithoutType`
  （`[]` 推不出元素类型）。写树的循环里不需要它，探针里改成把着法拼成字符串绕开。
- **范围缩小**：没有 policy/value 网络与 self-play，理由见上；`search.cml` 里没有任何
  "为了绕开缺陷而变弱"的地方——搜索是真的搜索，5000 次试走建出 4929 个结点。

## 测试

- `test/plans/feat/showcase_d4.plan.toml`（fast）：4 个搜索算例各跑四个后端，加上三个拒绝
  算例（试走 0 次、不可能的局面、已经结束的局面），19 个用例。stdout 逐字节收紧，另外用
  `verify_script` 断言那些"搜索错了就会变"的性质：现在就能赢的着法均值正好 1.0、其它着法
  一定小于 1、所有孩子的访问数加起来等于 `playouts`、空棋盘上中心均值最高、试走 0 次必须
  非零退出且**不能**打印选中着法。
- `test/plans/perf/showcase_d4.plan.toml`（slow）：跑 `compare.py --profile small`，
  整条计划约 8 秒；任何一行不同就失败。

## 不提交的东西

`artifacts/`、`__pycache__/` 被 `.gitignore` 排除；`RESULTS.md` 提交。