# 宿主上的 `ccache` 丢了它的 exec 目标：`[code=127]` 不是测试红

**一句话**：`88zjwei` 上 `~/camel-work/bin/ccache` 是个两行的 wrapper，它的 exec 目标 `~/camel-work/libexec/ccache` 被删掉了；于是**任何真正要编 C++ 的闸门**都在编译第一步拿到 `[code=127]`，`ninja: build stopped`，`BUILD_EXIT=1`。**它不是测试失败，也不是谁改坏了代码。**

## 1. 症状（认这两行就够了）

```
/bin/sh: 1: ccache: not found
FAILED: [code=127] modules/transforms/CMakeFiles/transforms_cmo_objects.dir/Release/jvp.cpp.o
...
ninja: build stopped: subcommand failed.
[FAIL] Error executing command: cmake --build . --config Release
BUILD_EXIT=1
```

三条容易误导人的地方：

- **报错的 `.cpp` 是谁的不重要**：127 落在哪个编译单元，只看谁恰好第一个要重编。它和那个文件的改动**没有关系**。
- **只改 stdlib/文档/用例的增量跑不会撞上它**：那些跑只要几秒、一次编译都不发生（本轮实测 `build 2.9–6.4 s`），所以同一台机器上"有的车道绿、有的车道 127"是正常的，别据此怀疑自己的改动。
- 宿主忙的时候更容易误判：本轮全量构建在 load 21–27 下 `build` 一项就要 100–900 s，`BUILD_EXIT=1` 很容易被当成"编不过"。

## 2. 修法（本轮实际做的，两步）

1. 把真正的二进制放回 wrapper 期望的位置：

   ```sh
   mkdir -p ~/camel-work/libexec
   cp -f ~/.local/libexec/ccache ~/camel-work/libexec/ccache
   chmod +x ~/camel-work/libexec/ccache
   ```

   注意 `~/.local/libexec/ccache` 直接跑会缺库：`error while loading shared libraries: libhiredis.so.0.14`。库在 `~/.local/lib/libhiredis.so.0.14`。

2. wrapper 的 `LD_LIBRARY_PATH` 补上那个目录（原来是 `$HOME/camel-work/lib`，那个目录里**没有** `libhiredis`）：

   ```sh
   #!/bin/sh
   LD_LIBRARY_PATH=$HOME/.local/lib:$HOME/camel-work/lib:$LD_LIBRARY_PATH \
       exec $HOME/camel-work/libexec/ccache "$@"
   ```

验收：`~/camel-work/bin/ccache --version` 答 `ccache version 4.5.1`。

**为什么值得修而不是绕过**：`camel-remote.ps1` 在 `command -v ccache` 成功时导出 `CAMEL_USE_CCACHE=1`，于是**生成的 build 目录里 `CMAKE_CXX_COMPILER_LAUNCHER=ccache` 是写进缓存的**。仅仅把 wrapper 改名躲开探测，已有 build 目录照样带着那个 launcher 继续 127；要绕开就得把每个检出目录的缓存项清空（本轮试过：一次失败的 `cmake` 重配会把 `build/` 的 ninja 文件删掉，重建更贵）。修二进制是一分钟的事，绕开是每个车道各自付一次。

## 3. 规矩

**任何车道看到 `[code=127] ccache: not found` / `ccache --version` 不答版本，一律先怀疑这一条，不要去改自己的 C++ 代码。** 判定只要一条命令：

```sh
~/camel-work/bin/ccache --version
```

答版本 → 与本条无关；答 `exec: ...: not found` 或 `cannot open shared object file` → 就是它，按第 2 节修，然后把这次的读数丢掉重跑（在坏 ccache 下拿到的 `BUILD_EXIT=1` 不是读数）。

## 4. 本轮的账

- 2026-11-04 12:0x–13:0x（`rank0point` @ 88zjwei，`-Tier fast -Jobs 4`）：先是 `jvp.cpp.o` 的 `[code=127]`，随后整棵树每个编译单元都 127；修好后同一条命令 `BUILD_EXIT=0`，`feat/docs_links` 21/21、`feat/transforms` 45 条 0 fail、`feat/transforms/split` 26/26。
- 中途为了绕开它，本车道清过一次 `~/camel-work/rank0point/build` 的 `CMAKE_*_COMPILER_LAUNCHER` 并重配，结果重配因缺 conan 工具链参数而中断、把 `build/` 的 ninja 文件删掉了 —— 说明**绕过比修更贵**，已在第 2 节写明。
- 同一时段多条车道报告过"编不过"，`127` 出现不止一次；本条是它们的共同根因。