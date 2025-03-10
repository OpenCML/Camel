# Camel 项目开发环境配置指南

[English](setup.en.md) | 中文简体

## 1. 项目简介

Camel 是一个采用现代 C++ 技术栈构建的 AI 编程语言，前端基于 Antlr4 实现（需要 Java 环境支持），并整合了 Python 和 Node.js 工具链以实现跨平台开发。本指南旨在帮助开发者快速搭建 Camel 项目的开发环境。

---

## 2. 技术栈要求

以下是 Camel 项目所需的技术栈及版本要求：

| 组件    | 最低版本 | 推荐版本 | 验证命令           |
| ------- | -------- | -------- | ------------------ |
| Python  | 3.9      | 3.11     | `python --version` |
| Node.js | 18       | 20 LTS   | `node -v`          |
| Java    | 11       | 21       | `java -version`    |
| Clang   | 19       | 20       | `clang --version`  |
| CMake   | 3.20     | 3.28     | `cmake --version`  |
| Conan   | 2.10     | 2.12     | `conan --version`  |
| Ninja   | 1.11     | 1.11     | `ninja --version`  |

确保以上组件安装并配置正确后方可开展项目开发。

---

## 3. 环境配置

以下为开发环境的配置步骤，覆盖 Windows、macOS 和 Linux 平台的具体操作。

---

### 3.1 脚本语言环境安装

#### 3.1.1 安装 Python

1. 前往 [Python 官网](https://www.python.org/downloads/) 下载并安装适合您操作系统的版本。
2. 安装时勾选 **Add Python to PATH** 选项。
3. 打开终端，执行以下命令验证安装是否成功：
   ```bash
   python --version
   ```

#### 3.1.2 安装 Node.js

1. 访问 [Node.js 官网](https://nodejs.org/en/) 下载最新版或 LTS 版本。
2. 安装完成后，在终端中运行以下命令验证安装：
   ```bash
   node --version
   npm --version
   ```

#### 3.1.3 安装 Java

1. 可从 [Oracle 官网](https://www.oracle.com/java/technologies/javase-jdk11-downloads.html) 或 [AdoptOpenJDK](https://adoptopenjdk.net/) 下载并安装 Java。
2. 配置 `JAVA_HOME` 和 `PATH` 环境变量。
3. 打开终端，运行以下命令确认安装：
   ```bash
   java -version
   ```

---

### 3.2 C++ 开发工具链

#### 3.2.1 Clang

1. **安装方法**：
   - **Windows**：从 [LLVM 官方下载页面](https://releases.llvm.org/download.html) 或 [GitHub Release](https://github.com/llvm/llvm-project/releases) 获取安装程序。
   - **macOS**：安装 Xcode 命令行工具：
     ```bash
     xcode-select --install
     ```
     或使用 Homebrew：
     ```bash
     brew install llvm
     ```
   - **Linux**：通过包管理器安装，例如：
     ```bash
     # Ubuntu/Debian:
     sudo apt update && sudo apt install clang
     ```
     若需要更高版本，可参考 [LLVM 官方文档](https://apt.llvm.org/) 添加官方仓库并安装。
2. **验证安装**：
   ```bash
   clang --version
   ```

---

#### 3.2.2 CMake

1. **安装方法**：
   - 从 [CMake 官方下载页面](https://cmake.org/download/) 获取适合系统的安装程序或压缩包。
   - 也可以使用系统包管理器安装：
     ```bash
     # macOS:
     brew install cmake

     # Ubuntu/Debian:
     sudo apt install cmake
     ```

2. **验证安装**：
   ```bash
   cmake --version
   ```

---

#### 3.2.3 Conan

1. **安装方法**：
   - 确保已安装 Python，然后使用 pip 安装 Conan：
     ```bash
     pip install conan
     ```

2. **验证安装**：
   ```bash
   conan --version
   ```

---

#### 3.2.4 Ninja

1. **安装方法**：
   - 从 [Ninja 官方 GitHub](https://github.com/ninja-build/ninja/releases) 下载可执行文件。
   - 或通过包管理器安装：
     ```bash
     # macOS:
     brew install ninja

     # Ubuntu/Debian:
     sudo apt install ninja-build
     ```
   - 或通过 pip 安装：
     ```bash
     pip install ninja
     ```

2. **验证安装**：
   ```bash
   ninja --version
   ```

---

### 3.3 包管理工具配置

#### 3.3.1 Conan 初始化配置
```bash
conan profile detect --force
```

#### 3.3.2 自定义构建配置

编辑 `~/.conan2/profiles/default` 文件，配置如下：
```ini
[settings]
os=Windows
arch=x86_64
compiler=clang
compiler.version=19
compiler.cppstd=23
build_type=Release

[conf]
tools.build:jobs=20
tools.cmake.cmaketoolchain:generator=Ninja Multi-Config
```

---

### 3.4 项目依赖安装

执行以下命令完成依赖安装：

1. 初始化依赖（仅需执行一次）：
   ```bash
   npm run init
   ```

2. 更新 Conan 依赖：
   ```bash
   npm run install
   ```

3. 若 Antlr4 语法定义有变更，重新生成解析器：
   ```bash
   npm run psrgen
   ```

---

### 3.5 编译目标构建

1. 构建 Release 版本：
   ```bash
   npm run build
   ```

2. 构建 Debug 版本（支持断点调试）：
   ```bash
   npm run debug
   ```

3. 清理构建产物：
   ```bash
   npm run clean
   ```

---

## 4. VSCode 配置推荐

### 4.1 基础配置

创建 `.vscode/settings.json` 文件：
```json
{
    "files.associations": {
        ".opencmlrc": "json"
    },
    "editor.tabSize": 4
}
```

### 4.2 调试配置

创建 `.vscode/launch.json` 文件：
```json
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
      "args": ["inspect", "--cst", "D:\\Projects\\Camel\\test\\format\\format.cml"],
      "console": "externalTerminal",
      "logging": {
        "moduleLoad": false,
        "trace": true
      },
    }
  ]
}
```

### 4.3 头文件提示配置

创建 `.vscode/c_cpp_properties.json` 文件：
```json
{
  "configurations": [
    {
      "includePath": [
        "${workspaceFolder}/src",
        "${workspaceFolder}/vendor",
        "${workspaceFolder}/third_party"
      ],
      "cppStandard": "c++23"
    }
  ]
}
```

---

## 5. 参考资源

1. [CMake 官方文档](https://cmake.org/documentation/)
2. [Conan 2.0 最佳实践](https://docs.conan.io/2/)
3. [LLVM Clang 工具链指南](https://clang.llvm.org/docs/UsersManual.html) 
4. [Node.js 官方文档](https://nodejs.org/en/docs/)
5. [Python 官方文档](https://docs.python.org/3/)
6. [Ninja 构建系统](https://ninja-build.org/manual.html)
7. [Antlr4 官方文档](https://www.antlr.org/)