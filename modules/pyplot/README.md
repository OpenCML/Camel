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
- Python 解释器进程内单例，先加载的模块完成 `Py_Initialize`
- 部署时将 `python*.dll` 置于 `libs/`，与 `libcamel.dll` 同目录（`collect-out` 已自动复制）

## 启动成本（H203）

`load()` 里的 CPython 初始化在 `inspect --ops registry_all.cml` 上量到 **9.26 ms**（该命令
`cmo_load` 18.47 ms 的 50%，整进程 42.5 ms 的 22%）；只导入 `pyplot` 的程序也要付
**9.33 ms**。**没有**把它推迟到第一次 `plot`：导入期的 Python 初始化失败是可见的退出码，
而且 `pyplot` 与 `python` 的 `load()` 初始化序列不同（`python` 会 promote libpython、设置
Python 输出顺序），先加载谁决定了解释器的设置。测量、失败用例与需要作者裁定的 P1/P2 见
[`docs/technical/50_pyplot_cpython_init.md`](../../docs/technical/50_pyplot_cpython_init.md)。
