# Contributing to Camel

[中文简体](docs/CONTRIBUTING.cn.md) | English

Thank you for your interest in the Camel project! We welcome all forms of contribution, including code, documentation, bug reports, and feature suggestions.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How to Contribute](#how-to-contribute)
- [Development Environment](#development-environment)
- [Development Workflow](#development-workflow)
- [Style Guide](#style-guide)
- [Commit Guidelines](#commit-guidelines)
- [Issue & PR Guidelines](#issue--pr-guidelines)

## Code of Conduct

By participating in this project, you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md). Please treat all community members with respect and inclusion.

## How to Contribute

1. **Report Bugs**: Open an issue on [GitHub Issues](https://github.com/OpenCML/Camel/issues) with the `bug` label
2. **Suggest Features**: Open an issue for discussion with the `enhancement` or `feature` label
3. **Code Contributions**: Fork the repository, make changes, and submit a Pull Request
4. **Documentation**: Welcome improvements—submit a PR directly

Issues labeled `beginner-friendly` are great for first-time contributors.

## Development Environment

See the [Setup Guide](docs/setup.en.md) ([中文](docs/setup.cn.md)) for detailed installation instructions. Required components:

| Component | Min. Version |
|-----------|--------------|
| Python | 3.9 |
| Node.js | 18 |
| Java | 11 |
| Clang | 19 |
| CMake | 3.20 |
| Conan | 2.10 |
| Ninja | 1.11 |

## Development Workflow

1. **Fork and clone**

   ```bash
   git clone https://github.com/YOUR_USERNAME/Camel.git
   cd Camel
   ```

2. **Install dependencies**

   ```bash
   pip install camel-lang
   npm install
   npm run init
   ```

3. **Build**

   ```bash
   npm run build
   ```

4. **Run tests**

   ```bash
   npm run test
   ```

5. **Before committing**

   ```bash
   npm run format    # Format C++ code
   npm run check     # Run checks
   ```

## Style Guide

- **C++ code**: Use `clang-format`—run `npm run format` for files under `src/` and `include/` (`.cpp`, `.h`)
- **Camel source**: Follow existing project syntax and style
- **Documentation**: Markdown format, consistent with existing docs

### Project Structure

- `src/`: C++ implementation
- `include/`: C++ headers
- `libs/`: Library modules (e.g., `python`)
- `test/`: Test cases
- `docs/`: Documentation

## Commit Guidelines

- Write clear, concise commit messages describing the change
- Reference issues with `Fix #123` or `Closes #123`
- [Conventional Commits](https://www.conventionalcommits.org/) style (optional):
  - `feat:` new feature
  - `fix:` bug fix
  - `docs:` documentation
  - `refactor:` refactoring
  - `test:` tests

## Issue & PR Guidelines

### Submitting Issues

- Search for existing issues before creating a new one
- Use appropriate labels (`bug`, `enhancement`, `question`, etc.)
- Include steps to reproduce, environment info, and expected behavior

### Submitting Pull Requests

1. Branch from the latest `main`
2. Keep PRs focused for easier review
3. Ensure CI passes
4. Request review from at least one maintainer

---

Thank you for contributing! 🐪 **Join the Herd** – Build the future of AI infrastructure with us!
