# 规则写成 Camel 模块的 Python 项目

[English](README.md) | 中文简体

一家小店的定价规则住在 [`rules.cml`](rules.cml) 里，包着它的项目是 Python：读订单、用 NumPy 存、
打印报表。这就是 H253 想走的采用路径——"在 Python 项目里用一部分 Camel"，不是把项目搬过去。

```sh
pip install camel-lang
python examples/python/rules/basket.py
```

`pip install` 是项目做一次的事，第二条命令是它平时跑的事。没有别的配置：wheel 把运行时与标准库
一起装在 `camel/.cmlenv/` 下，包自己把 `CAMEL_HOME` 指过去（`camel/_native.py`）。绑定的各个面
（数组的进出、异常、扩展模块）记在[技术文档 57](../../../docs/technical/57_python_bindings.md)。

## 运行输出

```
rules module: rules from examples/python/rules/rules.cml

  basket    discount   shipping   net total   net (Python)
     80.00     0.00%      9.90       89.90          89.90
    120.00     5.00%      9.90      123.90         123.90
    480.00     5.00%      0.00      456.00         456.00
    620.00    10.00%      0.00      558.00         558.00
   1200.00    15.00%      0.00     1020.00        1020.00
   2500.00    20.00%      0.00     2000.00        2000.00

1,000,000 baskets, one NumPy array of f32 (4 MiB)
  Camel:          1264.1 ms, rules as functions in rules.cml
  plain Python:    443.2 ms, the same rules written again here
  NumPy:            29.9 ms, the same rules as array expressions
  agreement:    Camel against Python 0.00e+00, against NumPy 0.00e+00 (largest net total here is 239999)
```

（时间取自参照 Linux 主机上的一次运行；两个 agreement 数字才是不会动的那个结论。）

## 这个示例主张什么、不主张什么

`rules.cml` 里的规则在**模块加载时**就被检查。规则里写错一个名字，是 `camel.load` 那一刻抛出的、
带文件、行、列的诊断，而不是一个月后在某个分支上才炸的运行时错误——测试套件把这件事的形状钉在
`test/cases/feat/bindings/python_binding.py` 里。数组入口收的是 DLPack capsule，所以上面那一百万
笔订单在进去的路上**没有**被复制：绑定自己的测试量到 64 MiB 交出去 12.9 us，而拷贝同样这些字节是
4818 us。

它**不**主张这是算一百万笔订单的快速办法。上面的数字说的正相反，而它们就摆在示例里，因为诚实的
答案就在里面：这套规则塞得进一个 NumPy 表达式，而 NumPy 比它快四十倍。模块跑在 node VM 上，那是
一个解释器，每个元素一次闭包调用，所以"就是一段紧凑算术"的规则该留在 NumPy。规则长到 NumPy 放
不下的时候（不是标量而是记录、规则本身会失败（`T!`）、规则调用规则、需要 `match` 的判断），选择
才变成"一条规则一个 Python 函数（运行时才检查）"对"一个 Camel 模块（加载时检查）"。那部分才值得
写成 Camel，而这个示例是把整条路走通的最小一个。