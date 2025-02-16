# Camel Project Development Environment Setup Guide

[中文简体](setup.cn.md) | English

## Project Introduction
Camel is an AI programming language built on a modern C++ technology stack. Its frontend is based on Antlr4 (which requires Java), and it integrates Python and Node.js toolchains for cross-platform development. This guide will help you quickly set up a complete development environment.

## Technology Stack Requirements
| Component      | Minimum Version | Recommended Version | Verification Command |
| -------------- | --------------- | ------------------- | -------------------- |
| Python         | 3.9             | 3.11                | `python --version`   |
| Node.js        | 18              | 20 LTS              | `node -v`            |
| Java           | 11              | 21                  | `java -version`      |
| Clang          | 19              | 20                  | `clang --version`    |
| CMake          | 3.20            | 3.28                | `cmake --version`    |
| Conan          | 2.10            | 2.12                | `conan --version`    |
| Visual Studio  | 2019            | 2022                | -                    |

> Visual Studio is only required on Windows platforms. When installing, you can also install the Clang component simultaneously.

## Environment Setup Process

### 1. Basic Toolchain Installation
#### 1.1 Scripting Language Environment
**Installing Python**

1. Visit the [Python official website](https://www.python.org/downloads/) to download and install Python.
2. During the installation, ensure to check **Add Python to PATH**.
3. After installation, open the command line and enter `python --version` to verify the installation.

**Installing Node.js**

1. Visit the [Node.js official website](https://nodejs.org/en/) to download and install Node.js.
2. After installation, open the command line and enter `node --version` and `npm --version` to verify the installation.

**Installing Java**

1. Download Java from [Oracle's official website](https://www.oracle.com/java/technologies/javase-jdk11-downloads.html) or [AdoptOpenJDK](https://adoptopenjdk.net/).
2. Install Java and set up the environment variables.
3. After installation, open the command line and enter `java -version` to verify the installation.

#### 1.2 C++ Development Toolchain
- **Windows Platform**
  1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/)
  2. Select components:
     - C++ Desktop Development Workload
     - Windows 10/11 SDK
     - Clang Compiler (recommended v17)
     - CMake Integration Support

- **Linux/macOS Platform**
  ```bash
  # Ubuntu/Debian
  sudo apt install clang-17 cmake ninja-build

  # macOS (Homebrew)
  brew install llvm@17 cmake ninja
  ```

#### 1.3 CMake Environment Verification
```bash
cmake -B build -S . -G "Visual Studio 17 2022"  # Windows
cmake -B build -S . -G "Ninja"                 # Unix-like
```

### 2. Package Management Tool Configuration
#### 2.1 Installing and Configuring Conan
```bash
pip install --upgrade conan==2.1.0

# Initialize configuration
conan profile detect --force
conan config set general.revisions_enabled=1

# Users in China may consider setting a mirror source
conan remote add conan-center https://mirrors.aliyun.com/conan-center
```

#### 2.2 Custom Build Configuration
Edit `~/.conan2/profiles/default`:
```ini
[settings]
os=Windows
arch=x86_64
compiler=clang
compiler.version=19
compiler.cppstd=23
build_type=Release

[conf]
tools.build:jobs=8  # Adjust according to CPU core count
tools.cmake.cmaketoolchain:generator=Visual Studio 17 2022
```

### 3. Project Dependency Installation
```bash
# Execute the following command to install dependencies (only needed once)
npm run init

# If Conan dependencies are updated, you can run the following command to install
npm run install

# If the Antlr4 grammar definitions are updated, you can run the following command to regenerate the parser
npm run psrgen
```

### 4. Build System Configuration
#### 4.1 Build Targets
```bash
# Release build
npm run build

# Debug build (supports breakpoint debugging)
npm run debug

# Clean build artifacts
npm run clean
```

#### 4.2 Multi-Platform Compilation Options
| Parameter    | Windows               | Linux/macOS |
| ------------ | --------------------- | ----------- |
| Generator    | Visual Studio 17 2022 | Ninja       |
| Compilation Parallelism | /MP                   | -j8         |
| Runtime Library | MD                    | libstdc++   |

## Additional Information

### 1. Common Issue Solutions
#### 1.1 VSCode Header File Detection
Execute the following command in the project root directory:
```bash
npm run fix:vsc
```

#### 1.2 ANTLR4 Runtime Linking Error
Execute the following command in the project root directory:
```bash
npm run fix:link
```

### 2. Recommended VSCode Configuration

#### 2.1 Basic Configuration
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

#### 2.2 Debug Configuration
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

#### 2.3 Hint Configuration
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
      "cppStandard": "c++23",
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

## Reference Resources
1. [CMake Official Documentation](https://cmake.org/documentation/)
2. [Best Practices for Conan 2.0](https://docs.conan.io/2/tutorial/consuming_packages.html)
3. [LLVM Clang Toolchain Guide](https://clang.llvm.org/docs/UsersManual.html)