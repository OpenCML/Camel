# npm 脚本说明

本文档说明项目根目录下 `package.json` 中定义的 npm 脚本及其用法。

## 常用脚本

| 脚本 | 说明 |
|------|------|
| `npm run build` | Release 构建，生成 `camel` 可执行文件 |
| `npm run debug` | Debug 构建 |
| `npm run check` | 代码健康检查 |
| `npm run format` | 代码格式化 |
| `npm run clean` | 清理构建产物 |

## test：交互式测试运行器

`npm run test` 会依次对 `test/run/<type>/` 目录下的 `.cml` 文件执行指定命令，支持交互式选择与执行。

### 用法

```bash
npm run test -- <type> [template]
```

- **type**：测试套件目录名，如 `linear`、`para`、`nn`，对应 `test/run/<type>/`
- **template**：命令模板，`{file}` 会被替换为实际文件路径；未指定时默认为 `run {file}`

### 示例

```bash
# 使用默认模板（camel run <file>）运行 linear 下的测试
npm run test -- linear

# 使用 inspect --gir 查看 GIR
npm run test -- linear "inspect --gir {file}"

# 指定 pass 阶段
npm run test -- linear "run {file} --pass-until 3"
```

### 交互按键

执行后进入交互模式，按键说明：

| 按键 | 说明 |
|------|------|
| **Enter** | 执行当前选中的文件 |
| **n** | 切换到下一个文件（不执行） |
| **p** | 切换到上一个文件（不执行） |
| **q** | 退出 |
| **c** | 清屏 |

提示框会随终端宽度自适应，界面文案为英文以保证等宽显示。

### 注意事项

1. 运行前需先执行 `npm run build` 生成 `camel` 可执行文件
2. **必须使用 `--`**：`npm run test -- linear "template"`，否则 npm 会消费 `-v`、`--log-level` 等参数
3. 若 npm 仍消费参数，可用环境变量兜底：

   ```powershell
   # PowerShell
   $env:TEST_TEMPLATE="-v --log-level debug {file} std::nvm"
   npm run test -- linear
   ```

   ```bash
   # Bash
   TEST_TEMPLATE="-v --log-level debug {file} std::nvm" npm run test -- linear
   ```
