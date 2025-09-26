# Camel Project Development Environment Setup Guide

[中文简体](setup.cn.md) | English

## 1. Project Overview

Camel is an AI programming language built with a modern C++ technology stack. Its frontend is implemented using Antlr4 (requires a Java runtime) and integrates Python and Node.js toolchains for cross-platform development. This guide is designed to help developers quickly set up the development environment for the Camel project.

---

## 2. Technical Requirements

The following table lists the required components and their version specifications for the Camel project:

| Component | Minimum Version | Recommended Version | Verification Command    |
| --------- | --------------- | ------------------- | ----------------------- |
| Python    | 3.9             | 3.11               | `python --version`      |
| Node.js   | 18              | 20 LTS             | `node -v`               |
| Java      | 11              | 21                 | `java -version`         |
| Clang     | 19              | 20                 | `clang --version`       |
| CMake     | 3.20            | 3.28               | `cmake --version`       |
| Conan     | 2.10            | 2.12               | `conan --version`       |
| Ninja     | 1.11            | 1.11               | `ninja --version`       |

Ensure that the above components are properly installed and configured before proceeding with development.

---

## 3. Environment Setup

The following steps outline the setup process for the development environment, covering Windows, macOS, and Linux platforms.

---

### 3.1 Installing Script Language Environments

#### 3.1.1 Installing Python

1. Visit the [Python official website](https://www.python.org/downloads/) to download and install the version suitable for your operating system.
2. During installation, check the **Add Python to PATH** option.
3. Open a terminal and run the following command to verify the installation:
   ```bash
   python --version
   ```

#### 3.1.2 Installing Node.js

1. Visit the [Node.js official website](https://nodejs.org/en/) to download the latest or LTS version.
2. After installation, verify it in the terminal:
   ```bash
   node --version
   npm --version
   ```

#### 3.1.3 Installing Java

1. Download and install Java from the [Oracle official website](https://www.oracle.com/java/technologies/javase-jdk11-downloads.html) or [AdoptOpenJDK](https://adoptopenjdk.net/).
2. Configure the `JAVA_HOME` and `PATH` environment variables.
3. Open a terminal and confirm the installation:
   ```bash
   java -version
   ```

---

### 3.2 C++ Development Toolchain

#### 3.2.1 Clang

1. **Installation Methods**:
   - **Windows**: Download from the [LLVM official download page](https://releases.llvm.org/download.html) or [GitHub Releases](https://github.com/llvm/llvm-project/releases).
   - **macOS**: Install via Xcode command line tools:
     ```bash
     xcode-select --install
     ```
     Or use Homebrew:
     ```bash
     brew install llvm
     ```
   - **Linux**: Install via package manager, e.g.:
     ```bash
     # Ubuntu/Debian:
     sudo apt update && sudo apt install clang
     ```
     For newer versions, refer to the [LLVM official documentation](https://apt.llvm.org/) to add the official repository and install.
2. **Verify Installation**:
   ```bash
   clang --version
   ```

---

#### 3.2.2 CMake

1. **Installation Methods**:
   - Download the installer or archive from the [CMake official download page](https://cmake.org/download/).
   - Alternatively, use a package manager:
     ```bash
     # macOS:
     brew install cmake

     # Ubuntu/Debian:
     sudo apt install cmake
     ```

2. **Verify Installation**:
   ```bash
   cmake --version
   ```

---

#### 3.2.3 Conan

1. **Installation**:
   - Ensure Python is installed, then use pip to install Conan:
     ```bash
     pip install conan
     ```

2. **Verify Installation**:
   ```bash
   conan --version
   ```

---

#### 3.2.4 Ninja

1. **Installation Methods**:
   - Download the executable from the [Ninja official GitHub](https://github.com/ninja-build/ninja/releases).
   - Or install via a package manager:
     ```bash
     # macOS:
     brew install ninja

     # Ubuntu/Debian:
     sudo apt install ninja-build
     ```
   - Alternatively, install via pip:
     ```bash
     pip install ninja
     ```

2. **Verify Installation**:
   ```bash
   ninja --version
   ```

---

### 3.3 Package Management Configuration

#### 3.3.1 Conan Initialization
```bash
conan profile detect --force
```

#### 3.3.2 Custom Build Configuration

Edit the `~/.conan2/profiles/default` file with the following configuration:
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

### 3.4 Installing Project Dependencies

Run the following commands to install the required dependencies:

1. Install project dependencies (automatically installs and initializes the project):
   ```bash
   npm install
   ```

2. Manually initialize the project (if needed):
   ```bash
   npm run init
   ```

3. If the Antlr4 grammar definitions have changed, regenerate the parser:
   ```bash
   npm run psrgen
   ```

4. For VSCode users, it is recommended to run the following command to fix Intellisense errors:
   ```bash
   npm run fix:vsc
   ```

---

### 3.5 Building Targets

1. Build the Release version:
   ```bash
   npm run build
   ```

2. Build the Debug version (for breakpoint debugging):
   ```bash
   npm run debug
   ```

3. Clean build artifacts:
   ```bash
   npm run clean
   ```

---

## 4. Reference Resources

1. [CMake Official Documentation](https://cmake.org/documentation/)
2. [Conan 2.0 Best Practices](https://docs.conan.io/2/)
3. [LLVM Clang Toolchain Guide](https://clang.llvm.org/docs/UsersManual.html)
4. [Node.js Official Documentation](https://nodejs.org/en/docs/)
5. [Python Official Documentation](https://docs.python.org/3/)
6. [Ninja Build System](https://ninja-build.org/manual.html)
7. [Antlr4 Official Documentation](https://www.antlr.org/)