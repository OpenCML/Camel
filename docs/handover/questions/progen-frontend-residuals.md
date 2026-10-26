# progen 差分跑在退役拼写修好之后剩下的四条

分支 `handover/progen-frontend`（`origin/develop` = `e16563e95` + 本分支只改生成器拼写与归档用例，前端行为未动）。
点名计划 `test/plans/feat/opt/progen.plan.toml` 的 `progen_check` 是 25 个种子 × 41 条管道 = 1025 格，
每格一次编译：`python test/tools/progen/progen.py --camel <install>/bin/camel check`。

修掉三种退役拼写（声明前缀 `sync func`、lambda 前缀 `sync (`、负区间端点加括号）之前是 1025 格全错
（100% 停在 parse 的 `Unexpected 'sync'`），之后是 **OK 802 / FRONTEND 123 / MISMATCH 100**，计划读数
`223 wrong`。123 格是种子 1、14、19 各 41 格（编译被拒），100 格是种子 5、6 各 41 格（无 pass 也错）加
种子 16 的 18 格。下面 (a)(b)(c) 是前两类的逐字读数；(d) 只作交叉引用。

## 量到的

**(a) 箭头接收者的数组字面量没进入下游调用给出的类型实参，报 `NoMatchingFunction`。**
种子 1（行号按 `progen.py show --seed 1` 写出的 `progen_1.cml`）`:31:13`：

```
return (([{ x: 100, y: 4 }, { x: 31, y: -1 }]->append<{ x: 12, y: 4 }>)->filter<(p8: P): bool => …>)
```

种子 14 `progen_14.cml:50:19`：`let n19 = [{ x: 7, y: 10 }]->map<(p18: P): P => 1*p18>;`
（`P = { x: int, y: int }` 是同一个程序自己的 `type`）两处都报：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <(p8: P) => bool pure> ({ x: int, y: int }[]).
help: … Available overloads:
          <:mark/filter_arr>: <f: (item: T) => bool> (xs: T[]) => T[]
```

实参类型打印成匿名结构 `{ x: int, y: int }[]`，不是 `P[]`：接收者的字面量没有进入 `append` 的显式类型实参
`{ x: 12, y: 4 }` 与 `filter` 的 `f: (item: T) => bool` 给出的 `P`。

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

**(a)** 该报，还是该把接收者字面量的元素类型推进下游调用给出的类型实参（`P`）？
**(b)** 这条是否符合裁定（若是，生成/归档程序该按元素类型改写）？
**(c)** 属前端还是 `std::opt` 的规则，归哪条车道？