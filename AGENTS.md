# Agent 使用指南

## 环境说明
- 工作环境为 Windows，命令连接请用 `;` 而非 `&&`

## 编译
- 普通：`npm run build`
- 调试：`npm run debug`
- 性能分析：`npm run profile`
- 产物目录：`out/latest/bin/`，主程序为 `camel.exe`

## 运行
- 依赖动态库：`libcamel.dll`
- 需配置 `CAMEL_HOME` 指向安装目录（含 bin、lib 等）

**CMD：**
```
set CAMEL_HOME=项目根目录\out\latest
set PATH=%PATH%;%CAMEL_HOME%\bin
```

**PowerShell：**
```
$env:CAMEL_HOME = "项目根目录\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
```

### 调试输出

`-v` 或 `--log-level debug` 可在 **debug 模式** 编译下执行时打印详细日志。

**注意**：输出量非常大。对于高调用次数的任务（如 `fib 30` 约 300 万次函数调用），**非常不建议**在 debug 模式下执行，更不建议开启调试输出，否则会长时间卡死或输出溢出。

**建议做法**：
- 使用 **build 模式** 编译并执行（此时 `-v` 不生效，适合正常跑测）
- 或缩小测试规模，如改用 `fib 3` / `fib 4` 逐步验证程序行为

## Passes 用法

基本格式：`camel xxx.cml pass1 pass2 ...`

`std::nvm` 是 fallback pass，即当用户没有指定任何pass，或者用户指定的passes处理完后没有得到空图，则继续送入 `std::nvm` 处理。

### 常见 Pass

| Pass | 说明 |
|------|------|
| `std::gir` | 转译遍，将当前 GIR 打印出来，返回空图 |
| `std::nvm` | 调度遍，节点虚拟机，线性执行，得到结果后返回空图 |
| `std::fvm` | 调度遍，高速字节码虚拟机，比 nvm 更快，返回空图 |
| `std::jit` | 调度遍，启用 JIT 的字节码虚拟机，速度最快，返回空图 |
| `std::inline` | 优化遍，将输入 GIR 中的小函数（子图）内联到大图中以减少函数调用开销，在高频分支和函数调用场景有一定作用，返回优化后的图 |

### 示例

```
camel fib.cml std::gir              # 仅打印 GIR
camel fib.cml std::inline std::fvm  # 先内联优化，再高速字节码虚拟机执行
```

## Agent 开发规范

- **TDD 测试驱动**：尽量采用测试驱动开发，先写测试再实现
- **测试用例**：多写测试样例，放在 `test/` 目录下
- **文档同步**：进行较大规模重构时，及时更新 `docs/` 中的文档
- **代码注释**：比较晦涩、复杂的函数必须有注释说明
