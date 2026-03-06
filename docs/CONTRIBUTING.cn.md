# 贡献指南 (Contributing to Camel)

[English](../CONTRIBUTING.md) | 中文简体

感谢你对 Camel 项目的关注！我们欢迎各种形式的贡献，包括但不限于代码、文档、Bug 报告和功能建议。

## 目录

- [行为准则](#行为准则)
- [如何贡献](#如何贡献)
- [开发环境配置](#开发环境配置)
- [开发流程](#开发流程)
- [代码规范](#代码规范)
- [提交规范](#提交规范)
- [Issue 与 PR 指南](#issue-与-pr-指南)

## 行为准则

参与本项目即表示你同意遵守我们的 [行为准则](../CODE_OF_CONDUCT.md)。请以尊重和包容的态度对待社区成员。

## 如何贡献

1. **报告 Bug**：在 [GitHub Issues](https://github.com/OpenCML/Camel/issues) 中提交，使用 `bug` 标签
2. **功能建议**：开启 Issue 讨论，使用 `enhancement` 或 `feature` 标签
3. **代码贡献**：Fork 本仓库，修改后提交 Pull Request
4. **文档改进**：欢迎改进文档，可直接提交 PR

标有 `beginner-friendly` 的 Issue 适合首次贡献者入门。

## 开发环境配置

请参阅 [开发环境配置指南](setup.cn.md)（[English](setup.en.md)），完成以下组件的安装：

| 组件 | 最低版本 |
|------|----------|
| Python | 3.9 |
| Node.js | 18 |
| Java | 11 |
| Clang | 19 |
| CMake | 3.20 |
| Conan | 2.10 |
| Ninja | 1.11 |

## 开发流程

1. **Fork 并克隆仓库**

   ```bash
   git clone https://github.com/YOUR_USERNAME/Camel.git
   cd Camel
   ```

2. **安装依赖**

   ```bash
   pip install camel-lang
   npm install
   npm run init
   ```

3. **构建项目**

   ```bash
   npm run build
   ```

4. **运行测试**

   ```bash
   npm run test
   ```

5. **提交前检查**

   ```bash
   npm run format    # 格式化 C++ 代码
   npm run check     # 代码检查
   ```

## 代码规范

### Style Guide

- **C++ 代码**：使用 `clang-format` 格式化，对 `src/` 和 `include/` 下的 `.cpp`、`.h` 文件执行 `npm run format`
- **Camel 源码**：遵循项目既有的语法和风格
- **文档**：Markdown 格式，保持与现有文档风格一致

### 文件结构

- `src/`：C++ 实现
- `include/`：C++ 头文件
- `libs/`：库模块（如 `python`）
- `test/`：测试用例
- `docs/`：文档

## 提交规范

- 提交信息清晰、简洁，说明改动内容
- 关联 Issue 时使用 `Fix #123` 或 `Closes #123`
- 遵循 [Conventional Commits](https://www.conventionalcommits.org/) 风格（可选）：
  - `feat:` 新功能
  - `fix:` Bug 修复
  - `docs:` 文档
  - `refactor:` 重构
  - `test:` 测试

## Issue 与 PR 指南

### 提交 Issue

- 在提交前搜索是否已有相关 Issue
- 使用合适的标签（`bug`、`enhancement`、`question` 等）
- 提供可复现的步骤、环境信息和预期行为

### 提交 Pull Request

1. 基于最新的 `main` 分支创建分支
2. 保持 PR 范围聚焦，便于审查
3. 确保 CI 通过
4. 请求至少一位维护者 Review

---

再次感谢你的贡献！🐪 **Join the Herd** – 与我们一起构建 AI 基础设施的未来！
