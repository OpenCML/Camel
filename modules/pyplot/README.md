# pyplot 模块

基于 matplotlib 的绘图模块，编译为 `pyplot.cmo`。与 `python` 模块共享 Python 运行时，使用相同的 Python DLL。

## 构建

在项目根目录执行 `npm run build`（或 `debug` / `profile`）后，构建产物包含 `pyplot.cmo`。需安装 Python 和 matplotlib：`pip install matplotlib`。

## 使用

```cml
import pyplot

func main() sync {
    let data = [1.0, 2.0, 3.0, 4.0, 5.0]
    pyplot.plot(data, "myplot.png")  // 可选第二个参数为输出文件名
    return 0
}
```

## 与 python 模块共享

- 两者均链接 `pybind11::embed` 和 `Python::Python`
- Python 解释器进程内单例：**第一次 `plot` 或 `py_*` 调用**创建它，先用到谁谁创建，另一个附着而不新建（共享入口 `modules/python/embed.h`；`import` 本身不初始化，见下）
- 部署时将 `python*.dll` 置于 `libs/`，与 `libcamel.dll` 同目录（`collect-out` 已自动复制）

## 启动成本与首次使用（H203 → H221）

`import pyplot` 不再初始化 CPython。2026-10-22 语言作者裁定：`import` 不执行代码（设计原则 3.1），
所以导入期初始化是违规的，**可以优化掉**——在第一次真正使用时初始化，不在导入时。今天 `load()` 只注册
执行器与算子的纯度；第一次 `plot` 才创建解释器（若 `python` 已经创建，则附着，不再新建）。导入期的
9.3 ms 因此从 `import pyplot` 消失，改由第一次 `plot` 支付。初始化失败的种类与退出码不变
（`RuntimeDiag::RuntimeError`、exit 1），但位置从导入处移到调用处，诊断文本写明 “at its first use”；
`inspect --ops` 也不再需要解释器就能列出 `pyplot:plot`。测量、两种导入顺序与把这条规则钉住的测试见
[`docs/technical/50_pyplot_cpython_init.md`](../../docs/technical/50_pyplot_cpython_init.md)。
