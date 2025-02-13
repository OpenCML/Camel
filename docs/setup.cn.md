

# Camel 项目开发环境配置指南

[English](setup.en.md) | 中文简体

## 项目简介
Camel 是一个基于现代 C++ 技术栈构建的AI编程语言，其前端基于Antlr4实现（需要Java），并结合 Python 和 Node.js 工具链实现跨平台开发。本指南将帮助您快速搭建完整的开发环境。

## 技术栈要求
| 组件          | 最低版本 | 推荐版本 | 验证命令           |
| ------------- | -------- | -------- | ------------------ |
| Python        | 3.9      | 3.11     | `python --version` |
| Node.js       | 18       | 20 LTS   | `node -v`          |
| Java          | 11       | 21       | `java -version`    |
| Clang         | 17       | 20       | `clang --version`  |
| CMake         | 3.20     | 3.28     | `cmake --version`  |
| Conan         | 2.0      | 2.1      | `conan --version`  |
| Visual Studio | 2019     | 2022     | -                  |

> Visual Studio 仅在 Windows 平台下需要安装，安装时可以同步安装 Clang 组件。

## 环境配置流程

### 1. 基础工具链安装
#### 1.1 脚本语言环境
**安装Python**

1. 访问 [Python官网](https://www.python.org/downloads/) 下载并安装Python。
2. 在安装过程中勾选 **Add Python to PATH**。
3. 安装后，打开命令行输入 `python --version` 验证安装。

**安装Node.js**

1. 访问 [Node.js官网](https://nodejs.org/en/) 下载并安装Node.js。
2. 安装后，打开命令行输入 `node --version` 和 `npm --version` 验证安装。

**安装Java**

1. 访问 [Oracle官网下载Java](https://www.oracle.com/java/technologies/javase-jdk11-downloads.html) 或 [AdoptOpenJDK](https://adoptopenjdk.net/)。
2. 安装Java并设置环境变量。
3. 安装后，打开命令行输入 `java -version` 验证安装。

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

# 如果Conan依赖有更新，可以执行以下命令安装
npm run install

# 如果Antlr4语法定义有更新，可以执行以下命令重新生成parser
npm run psrgen
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
| 参数       | Windows               | Linux/macOS |
| ---------- | --------------------- | ----------- |
| 生成器     | Visual Studio 17 2022 | Ninja       |
| 编译并行数 | /MP                   | -j8         |
| 运行时库   | MD                    | libstdc++   |

## 其他说明

### 1. 常见问题解决方案
#### 1.1 VSCode 头文件检测
在项目根目录执行命令：
```bash
npm run fix:vsc
```

#### 1.2 ANTLR4 运行时链接错误
在项目根目录执行命令：
```bash
npm run fix:link
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
      "args": ["--format", ".\\test\\format\\format.cml"],
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
        "${workspaceFolder}/third_party",
      ],
      "defines": [],
      "cStandard": "c11",
      "cppStandard": "c++20",
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
