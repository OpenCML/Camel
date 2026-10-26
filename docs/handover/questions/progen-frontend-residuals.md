# progen 差分跑在退役拼写修好之后剩下的四条

分支 `handover/progen-frontend`（`origin/develop` = `e16563e95` + 本分支只改生成器拼写与归档用例，前端行为未动）。
点名计划 `test/plans/feat/opt/progen.plan.toml` 的 `progen_check` 是 25 个种子 × 41 条管道 = 1025 格，
每格一次编译：`python test/tools/progen/progen.py --camel <install>/bin/camel check`。

修掉三种退役拼写（声明前缀 `sync func`、lambda 前缀 `sync (`、负区间端点加括号）之前是 1025 格全错
（100% 停在 parse 的 `Unexpected 'sync'`），之后是 **OK 802 / FRONTEND 123 / MISMATCH 100**，计划读数
`223 wrong`。123 格是种子 1、14、19 各 41 格（编译被拒），100 格是种子 5、6 各 41 格（无 pass 也错）加
种子 16 的 18 格。下面 (a)(b)(c) 是前两类的逐字读数；(d) 只作交叉引用。

与另一条车道的 `docs/handover/questions/holes-in-non-literal-positions.md`（随 `1284da992` 落地）的关系：那一页问的是
**洞**（没有写下元素或字段）与"字面量不进入位置"的边界；下面 (a)(b) 的元素都写下来了，是另一件事——(a) 已经量到
实参位通过、接收位不通过，所以它不是那一页第 1 条的重问，(b) 的两条道路也各不相同。

## 量到的

**(a) 接收位（`->map<…>` / `->filter<…>`）不认元素类型的两个写法是同一个类型。**
种子 1（行号按 `progen.py show --seed 1` 写出的 `progen_1.cml`）`:31:13`：

```
return (([{ x: 100, y: 4 }, { x: 31, y: -1 }]->append<{ x: 12, y: 4 }>)->filter<(p8: P): bool => …>)
```

种子 14 `progen_14.cml:50:19`：`let n19 = [{ x: 7, y: 10 }]->map<(p18: P): P => 1*p18>;`
（两个程序第 5 行都有 `type P = { x: int, y: int }`）两处都报：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <(p: P) => P pure> ({ x: int, y: int }[]).
help: … Available overloads:
          <:mark/map_arr>: <f: (item: T) => U> (xs: T[]) => U[]
```

实参类型打印成匿名结构 `{ x: int, y: int }[]`，lambda 的形参类型是 `P`，而 `P` 就是同一个结构：`T` 有两个写法。
四条最小对照（`camel check`，行号按本车道的临时探针，量完即删）：

- 实参位 `func f(xs: P[]): int` 上的 `f([{ x: 1, y: 2 }])`：**通过**（exit 0）——位置给写下的元素它的元素类型，这一路走到了。
- 接收位 `[{ x: 7, y: 10 }]->map<(p: P): P => p>`：报上面那条（`:6:13`）。
- 同一个接收位、lambda 写匿名拼写 `->map<(p: { x: int, y: int }): { x: int, y: int } => p>`：**通过**（exit 0）。
- `([{ x: 1, y: 2 }]->append<{ x: 3, y: 4 }>)->filter<(p: P): bool => true>`：报 `<(p: P) => bool pure> ({ x: int, y: int }[])`（`:6:13`）。

⇒ 不是"字面量不进入位置"：实参位进入了，接收位在 lambda 写匿名拼写时也进入了；差别只在 `P` 与
`{ x: int, y: int }` 这两个写法在接收位没有合成一个 `T`。

**(b) `for … in [字面量]` 的数组字面量取到 loop 结果的元素类型。**
种子 19 `progen_19.cml:50:40`（`c17: float`，caret 落在 `[c17, c17, 3.0]` 的 `c17` 上）：

```
return [loop s20 = a18 for q21 in [c17, c17, 3.0] do a18, int(1.0), match a18 { … }, (-3) + a18];

error[SemanticDiag::ElementTypeMismatch]: Cannot insert data of type 'float' into a(n) array with element type 'int'.
```

`[c17, c17, 3.0]` 是 `float[]`，却按 loop 结果（`a18`、`int(1.0)`、`(-3) + a18` 给 `int`）的元素类型检查。

**(c) `std::opt` 那一组下的效果顺序错（18 格）。**
种子 16（env `PROGEN_I0=5, PROGEN_I1=4, PROGEN_I2=5`）参考输出是
`L3 5 / L3 5 / q33 0 / q33 10 / L4 4 / L5 5 / L6 false / L7 true / L8 false`；
在 `simplify@{nvm,fvm,jit,prl}`、`opt@{nvm,fvm,jit,prl}`、`macro+opt@{nvm,fvm,jit,prl}`、
`inline+simplify@nvm`、`macro+simplify@nvm`、`simplify+simplify@nvm`、`opt+opt@nvm`、`inline+opt@nvm`、
`devirt+inline+dce@nvm` 共 18 格拿到 `q33 10 / L3 5 / L3 5 / q33 0 / …`：`q33 10` 从第 4 位挪到第 1 位。
`none`、`inline`、`inline*3` 与 `fold` 各格都对。

**(d) 交叉引用（不问）**：种子 5、6 各 41 格在**无 pass** 下少打印三条连续 `println`（`var v73 = …` 之后），
编译器自己发 `EffectNotExecuted` 并写 "This is legal"；`d7e912cb3` 的提交信息已把它登记为"另派车道"的那个缺陷。

## 请裁定

**(a)** 接收位要不要把 `T` 的两个写法合成一个（`P` 与 `{ x: int, y: int }` 出自同一条 `type` 声明）？若不要，
生成与归档程序就该把 lambda 的形参写成元素类型自己的拼写（这样接收位与实参位都通过）。
**(b)** 这条是否符合裁定（若是，生成/归档程序该按元素类型改写）？
**(c)** 属前端还是 `std::opt` 的规则，归哪条车道？