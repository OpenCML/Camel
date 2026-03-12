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
