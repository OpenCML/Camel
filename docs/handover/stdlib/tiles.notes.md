# tiles 重写说明

稿件：`tiles.cml`、`tiles.signatures.cml`（换进 `stdlib/signatures/tiles.cml`）。未编译。依据 `docs/design/gpu/README.cn.md` 第 2、3、6 节。

## 改了什么

1. **放置是标签，不是调用**：模块说明改写为 `#gpu`/`#cpu`（括号写法，标在函数、块或命名空间块上）、`--devices` 选卡、`tiles::compile` 读标签；`@gpu` 与 `run`、`run_device` 已删除，文中不再提。范围内不支持的算子是编译期错误，不回退。
2. **一个资源 `driver`**：`export resource driver`，模块里每个函数 `uses driver`（C++ 现在用 `tiles:driver` 与 `tiles:device` 两个，设备内存是句柄类型 `Device<...>` 的每个值各自的资源，签名文件里的 `uses driver` 是下限，`upload`/`download` 的设备值访问由 `Device` 句柄类型承担，见开放问题 2）。删 `@effect`。
3. **`device()` 返回 `string!`**：没有 CUDA 设备是环境造成的失败，种类 `DeviceUnavailable`（原来是故障）。`--devices` 点名了不存在的卡仍是启动时的校验，不是 `false`、也不是这里的错误。`available()` 不变。
4. **`compile` 遍的失败是错误值**：`GpuScopeUnsupported`（指出算子与范围），遍的返回类型本来就是 `F!`。
5. **`run_preferred_resident`**：设计稿第 6 节第 3 条保留为 `tiles::compile` 与测试使用的内部入口。仍留在签名文件里，说明改成"不是程序请求设备的办法"。它已经符合 `with` 判据（`inputs->run_preferred_resident<fn>`：函数是配置，输入是数据），形式不变；`source`、`inspect`、`benchmark`、`regions` 同。
6. 例子改成管道写法（`inputs->inspect<fn>`），去掉 `println(...)` 包裹。

## 函数表

| 项 | 处理 |
|---|---|
| `available(): bool` | 保留，`uses driver` |
| `device(): string!` | 改（原故障） |
| `compile`（遍） | 保留，`uses driver`，失败是 `F!` |
| `upload`、`download` | 保留（计算签名，`Device<...>`）；`uses driver` |
| `run_preferred_resident`、`source`、`inspect`、`benchmark`、`regions` | 保留（计算签名） |
| `resource driver` | 新增 |

## 为什么它们仍是计算签名

结果类型取自实参的叶子结构（`Device<...>` 逐叶包装）和任意参数表的函数类型（`with <fn: (...) => ...>`），都是"只有语言作者能答的缺口"（conventions 8）。设计稿 3 节要把设备上的数组并入普通数组值；原则 13.3 保留 `Device<T>`，两者的关系（是否去掉 `Device` 包装）是语言作者要定的事，本稿按原则 13.3 保留 `Device<T>`。

## C++ 侧改动（`modules/tiles/src/module.cpp`）

- 资源声明：`tiles:driver` 并入 `driver`（`export resource driver` 在 `.cml`，C++ 的 `declareResources` 引用它）；`tiles:device` 保留为 `Device` 句柄的外部资源。
- `device` 算子：失败改 `makeError("DeviceUnavailable", ...)`，结果 `string!`。
- `compile` 遍：`GpuScopeUnsupported` 由诊断改为遍的错误值（同一条消息）。
- 其余不动。

## 调用点

| 变化 | 位置 |
|---|---|
| `device()` 处理 `!`（2 处） | `test/cases/modules/tiles/vector_add.cml`、`test/cases/compile/native/tiles_declaration_import.cml` |
| `tiles::compile` 等遍的写法不变（约 10 处） | `test/cases/modules/tiles/`、`docs/` |
| 其余不变（约 119 个文件用 tiles，函数的调用形式没变） | — |

## 开放问题

1. **`#cpu` 标签**：设计稿 G1 写明"仍未做"。模块说明里按"已定的目标"写了；实现前这句话是目标不是现状，接入时按实际删减。
2. **`Device<T>` 句柄与按对象的资源**：每个设备值是各自的资源（`#resource` 句柄类型，3.3），`upload`/`download` 应写 `uses value`（形参名）。计算签名的参考文字里没法为每个叶子写；留给 C++ 的 `ResourceSpec`，文档里只写 `driver`。
