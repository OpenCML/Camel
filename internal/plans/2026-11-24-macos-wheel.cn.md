# camel-lang 发布 macOS arm64 包

负责人 2026-11-24：Mac mini 上 `pip install camel-lang` 找不到合适的版本；解决，随下一版发布（不进 0.2.46）。

## 现状

- `.github/workflows/build-pypi.yml` 只打 Linux x64、Linux arm64、Windows x64；`build-wheel-native` 里 macOS arm64 那一格是注释掉的，注释写明缺的是打包最后一步（5.37 第 6 项）。
- `ci.yml` 的 macOS arm64 腿一直在构建、跑测试，编译与运行都通。
- JIT 只有 x86-64 的代码生成（`cmake/Options.cmake` 在其他处理器上关掉 `ENABLE_FASTVM_JIT`），所以 arm64 上本来就没有运行时生成代码，不涉及 macOS 对 JIT 的签名与授权。
- 标签：`scripts/pypi-rollup.js` 在 `MACOSX_DEPLOYMENT_TARGET=13.0` 下已经给出 `macosx_13_0_arm64`。Intel Mac 不发布（负责人既有决定）。

## 要做的

1. **打包步骤**：在 macOS 那一格，原生构建和两个包（abi3 带 Python 扩展的、`py3-none` 只有本体的）打出来之后，用 `delocate-wheel` 把非系统动态库复制进包、改写加载路径。`delocate` 自己重新打包、重算 `RECORD`；先核实当前版本是否会给改过的 Mach-O 补临时签名，不会就在重新打包前对每个改过的文件跑 `codesign --force --sign -`，再让 `RECORD` 与文件内容一致（`wheel unpack` / `wheel pack` 重算）。
2. **检查**：`scripts/check_wheel_tags.py --ruling` 对 macOS 包同样适用；另加一条：包里每个 Mach-O 的依赖要么是系统库（`/usr/lib`、`/System`），要么在包内（`otool -L` 逐个核），每个都有有效签名（`codesign --verify`）。
3. **放开矩阵那一格**，`smoke` 一类的安装后冒烟在 macOS 上也跑：新建虚拟环境 `pip install` 这个包，`camel --version`、跑一个示例程序、`import camel` 走一次 Python 互操作。
4. **验证路径**：先 `workflow_dispatch` 发到 TestPyPI；负责人在 Mac mini 上 `pip install -i https://test.pypi.org/simple/ camel-lang` 装一次，跑同样的冒烟。通过后随下一版正式发布。

## 验收

- 一次 `build-pypi.yml` 运行产出两个 `macosx_13_0_arm64` 包，检查全过，冒烟全过。
- TestPyPI 上的包在一台没有开发环境的 Mac 上装得上、跑得起来（负责人的 Mac mini）。
- 注释里"NOT built yet"那段换成说明这一步怎么做的文字；`ci.yml` 那句"build-pypi.yml builds no macOS wheel yet"同步改掉。
