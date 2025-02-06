

# Camel 项目开发环境配置指南

## 项目简介
Camel 是一个基于现代 C++ 技术栈构建的软件开发项目，结合 Python 和 Node.js 工具链实现跨平台开发。本指南将帮助您快速搭建完整的开发环境。

## 技术栈要求
| 组件          | 最低版本   | 推荐版本   | 验证命令         |
|---------------|------------|------------|------------------|
| Python        | 3.9        | 3.11       | `python --version` |
| Node.js       | 18         | 20 LTS     | `node -v`         |
| Clang         | 15         | 17         | `clang --version` |
| CMake         | 3.20       | 3.28       | `cmake --version` |
| Conan         | 2.0        | 2.1        | `conan --version` |
| Visual Studio | 2019       | 2022       | -                |

> Visual Studio 仅在 Windows 平台下需要安装，安装时可以同步安装 Clang 组件。

## 环境配置流程

### 1. 基础工具链安装
#### 1.1 脚本语言环境
```bash
# 验证Python环境
python -m ensurepip --upgrade

# 设置Node.js镜像源（国内用户建议）
npm config set registry https://registry.npmmirror.com
# 安装Node.js依赖
npm install --force --legacy-peer-deps
```

#### 1.2 C++ 开发工具链
- **Windows 平台**
  1. 安装 [Visual Studio 2022](https://visualstudio.microsoft.com/)
  2. 选择组件：
     - C++ 桌面开发工作负载
     - Windows 10/11 SDK
     - Clang 编译器（推荐 v17）
     - Cmake 集成支持

- **Linux/macOS 平台**
  ```bash
  # Ubuntu/Debian
  sudo apt install clang-17 cmake ninja-build

  # macOS (Homebrew)
  brew install llvm@17 cmake ninja
  ```

#### 1.3 CMake 环境验证
```bash
cmake -B build -S . -G "Visual Studio 17 2022"  # Windows
cmake -B build -S . -G "Ninja"                 # Unix-like
```

### 2. 包管理工具配置
#### 2.1 Conan 安装与配置
```bash
pip install --upgrade conan==2.1.0

# 初始化配置
conan profile detect --force
conan config set general.revisions_enabled=1

# 国内用户可以考虑设置镜像源
conan remote add conan-center https://mirrors.aliyun.com/conan-center
```

#### 2.2 自定义构建配置
编辑 `~/.conan2/profiles/default`：
```ini
[settings]
os=Windows
arch=x86_64
compiler=clang
compiler.version=17
compiler.cppstd=20
build_type=Release

[conf]
tools.build:jobs=8  # 根据CPU核心数调整
tools.cmake.cmaketoolchain:generator=Visual Studio 17 2022
```

### 3. 项目依赖安装
```bash
# 执行以下命令一键安装（仅需执行一次）
npm run init
```

### 4. 构建系统配置
#### 4.1 构建目标
```bash
# Release构建
npm run build

# Debug构建（支持断点调试）
npm run debug

# 清理构建产物
npm run clean
```

#### 4.2 多平台编译选项
| 参数               | Windows               | Linux/macOS       |
|--------------------|-----------------------|-------------------|
| 生成器             | Visual Studio 17 2022 | Ninja             |
| 编译并行数         | /MP                  | -j8               |
| 运行时库           | MD                   | libstdc++         |

## 其他说明

### 1. 常见问题解决方案
#### 1.1 VSCode 头文件检测
在项目根目录执行命令：
```bash
npm run fix
```

#### 1.2 ANTLR4 运行时链接错误
在项目根目录执行命令：
```bash
npm run fix
```

### 2. VSCode 推荐配置

#### 2.1 基础配置
```json
// .vscode/settings.json
{
    "files.associations": {
        ".opencmlrc": "json",
    },
    "cSpell.words": [
        "antlr",
        "clipp",
        "DREF",
        "FSTRING",
        "hashable",
        "IDCL",
        "Idents",
        "ifexpr",
        "MEBA",
        "MEMA",
        "NREF",
        "ODCL",
        "ONNX",
        "opers",
        "reparent",
        "RETN",
        "Stmts",
        "typeas",
        "UNPK",
        "VARI",
        "wptr",
        "Zhenjie",
        "opencml",
        "opencmlrc"
    ],
    "editor.indentSize": "tabSize",
    "editor.tabSize": 4,
}
```

#### 2.2 调试配置
```json
// .vscode/launch.json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "C++ Debug (Windows)",
      "cwd": "${workspaceFolder}",
      "type": "cppvsdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/Debug/camel.exe",
      "symbolSearchPath": "${workspaceFolder}/build/Debug",
      "args": ["--format", "D:\\Projects\\Camel\\test\\format\\format.cml"],
      "console": "externalTerminal",
      "logging": {
        "moduleLoad": false,
        "trace": true
      },
    }
  ]
}
```

#### 2.3 提示配置
```json
// .vscode/c_cpp_properties.json
{
  "configurations": [
    {
      "name": "Win32",
      "includePath": [
        "${workspaceFolder}/src",
      ],
      "defines": [],
      "cStandard": "c11",
      "cppStandard": "c++17",
      "browse": {
        "path": ["${workspaceFolder}"],
        "limitSymbolsToIncludedHeaders": true,
        "databaseFilename": ""
      }
    }
  ],
  "version": 4
}
```

## 参考资源
1. [CMake 官方文档](https://cmake.org/documentation/)
2. [Conan 2.0 最佳实践](https://docs.conan.io/2/tutorial/consuming_packages.html)
3. [LLVM Clang 工具链指南](https://clang.llvm.org/docs/UsersManual.html)
