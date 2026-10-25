# 问题：`test/cases/facts/cost_many_tensor.cml` 没有任何计划引用它，该把它接进计划还是删掉

事实与读数，不含该删该留的判断。开发线 tip `0a1a509f5`。

## 1. 没有引用

把 `test/plans/**/*.plan.toml` 和全树的 `*.toml,*.mjs,*.js,*.json,*.md,*.cml,*.txt` 都 grep 一遍 `cost_many_tensor`，只命中它自己的第 5 行 `module cost_many_tensor`：没有计划把它当 `case`，也没有别的用例 `import` 它。

## 2. 它自己的说法

文件头两行写着：`// H158 measurement workload: N groups of three tensor function graphs (a matmul, a loop and a reduction) plus main.`

## 3. 读数

元素类型那次迁移之后它编译不过（字面量数组推断成 f64，而它的函数要 `Tensor<f32>`）：

```
camel test/cases/facts/cost_many_tensor.cml std::nvm
    --> test/cases/facts/cost_many_tensor.cml:319:17
 319 |     println(lp06(q))
     |                 ^^^
help: … Available overloads: lp06: (a: Tensor<float32>) => Tensor<float32>
camel: 20 errors; stopped at the limit of 20 errors
exit=1
```

`main` 里那三个字面量数组（第 297-299 行）补上 `as f32[]`（这次迁移在兄弟用例上的同一种写法）之后：`exit=0`、stdout 120 行、打印的 tensor 是 `dtype=float32`。

## 4. 要问的

1. 这个测量工作负载该接进一个计划（哪个？跑哪条 pass 序列？它今天只打印、没有 pin 任何值），还是删掉？
2. 树里是否还有别的用例文件同样没有任何计划引用？本文件只查了这次排查点到的那几个候选，没做全量普查。

（背景：`agents/test.md` 的两条——"a guard must be able to fail" 与 "a guard that examined zero things fails"——都默认一个用例至少被一个计划跑到；一个没有计划引用的用例坏了不会有任何东西变红，这次就是。）