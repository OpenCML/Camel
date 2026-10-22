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

See the [Setup Guide](docs/setup.en.md) ([中文](docs/setup.cn.md)) for detailed installation instructions. Components and minimum versions:

| Component | Min. Version |
|-----------|--------------|
| Python | 3.9 |
| Node.js | 18 |
| Clang | 19 |
| CMake | 3.25 |
| Conan | 2.10 |
| Ninja | 1.11 |

Clang, CMake, Conan and Ninja are required for the build itself. Python is needed only for the `python`/`pyplot` modules and the wheel: the build skips those modules when it finds no CPython with development headers, and `CAMEL_SKIP_PYTHON=1` forces the skip.

## Development Workflow

1. **Fork and clone**

   ```bash
   git clone https://github.com/YOUR_USERNAME/Camel.git
   cd Camel
   ```

2. **Install dependencies**

   ```bash
   npm install          # npm packages; its prepare step runs conan install
   ```

   `npm install` runs `scripts/install.js` (Conan) through the `prepare` hook. Run `npm run init` only when `conanfile.txt` or the CMake options change: it deletes `build/` and runs `conan install` again.

3. **Build**

   ```bash
   npm run build
   ```

4. **Run tests**

   ```bash
   npm run test        # the fast layer, against the build in out/latest
   ```

   `npm run test:all` adds the slow plans; see [docs/test-plans.md](docs/test-plans.md).

5. **Before committing**

   ```bash
   npm run format    # format C++ sources under src/, include/, modules/ and tools/
   npm run test      # fast test layer
   ```

   The pre-commit hook formats and updates the staged files automatically (it refuses a partially staged file, so stage whole files). `npm run check` is not a linter: it runs the housekeeping pass (format, refresh the `Updated:` header dates, print code counts) and rewrites files, so review the diff if you run it.

## Style Guide

- **C++ code**: Use `clang-format`—run `npm run format` for files under `src/`, `include/`, `modules/` and `tools/` (`.cpp`, `.h`)
- **Camel source**: Follow existing project syntax and style
- **Documentation**: Markdown format, consistent with existing docs

### Project Structure

- `src/`: C++ implementation
- `include/`: C++ headers
- `modules/`: Library modules (e.g., `python`)
- `stdlib/`: Standard library written in Camel
- `test/`: Test cases
- `docs/`: Documentation

## Commit Guidelines

- Use [Conventional Commits](https://www.conventionalcommits.org/) prefixes:
  - `feat:` new feature
  - `fix:` bug fix
  - `docs:` documentation
  - `refactor:` structural change with no behaviour change
  - `test:` tests
  - `chore:` maintenance
- Reference issues with `Fix #123` or `Closes #123`.
- The subject describes the whole staged change, not the last edit; a merge commit describes the work it integrates.

## Issue & PR Guidelines

### Submitting Issues

- Search for existing issues before creating a new one
- Use appropriate labels (`bug`, `enhancement`, `question`, etc.)
- Include steps to reproduce, environment info, and expected behavior

### Submitting Pull Requests

1. Branch from the latest `develop`. `main` is release-only: never commit to `main` or `develop` directly
2. Keep PRs focused for easier review
3. Run `npm run test` locally before opening the PR. No CI runs on pull requests: `.github/workflows/ci.yml` builds and tests on release tags (`v*`) and on manual dispatch, so a PR is not gated automatically
4. Request review from a maintainer. A reviewed PR is merged into `develop` with a merge commit (`git merge --no-ff`), not squashed or rebased; the merge commit summarizes the integrated work

---

Thank you for contributing! 🐪 **Join the Herd** – Build the future of AI infrastructure with us!
