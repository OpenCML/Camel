# CI 的 Linux 腿要不要装 CUDA Toolkit 头文件

## 量到的事实（2026-10-24）

三条 CI 腿（linux-x64、linux-x64-nojit、win-x64）在 configure 时都自报：

    -- tiles: source inspection only (CUDA Toolkit not found)

`modules/tiles/CMakeLists.txt:23` 是 `find_package(CUDAToolkit QUIET)`：找不到就不定义
`CAMEL_TILES_CUDA`，于是 `modules/tiles/src/cuda.cpp:882` 起的 `#else` 分支生效——`available()` 直接
返回 `false`（不问驱动），`deviceInfo()` 直接抛 `BackendUnavailable`（不构造后端）。**CI 上没有任何
一次真正的设备探测**。

后果：`test/plans/feat/modules/tiles.plan.toml` 里所有真设备条目在 CI 上 skip
（`have-cuda-device.mjs` 的 `tiles:available()` 为 false，日志里
`precondition not met ...: CUDA device 0 is not available (tiles:available() is false)`），而
`gpu_probe.once_per_process` 断言的那行日志在无后端构建里根本不会出现——它本来在 CI 上注定红，
本车道给它加了构建能力前置条件（见 `reports/ci-failures.md`）。也就是说：**CI 的 tiles 只测到
“没有 CUDA 时它也给出正确答案”，测不到设备路径本身**，而设备路径只在 88 主机上被跑。

## 选项与代价

| 选项 | 代价 | 换来什么 |
|---|---|---|
| （a）linux-x64 腿装 CUDA Toolkit 头文件（CUDA apt 源或 `nvidia-cuda-toolkit`） | 约 1-3 GB 安装、configure 与缓存键变化、`tiles` 的 CUDA 目标编进来（编译时间上去）；runner 无驱动，所以 `available()` 仍为 false，**设备真跑的条目仍然 skip** | `gpu_probe.once_per_process` 这类“构建能力”断言真的被执行；`CAMEL_TILES_CUDA` 路径的**编译**得到覆盖（这正是 10-24 之前没人拦住的类型） |
| （b）单独一个 opt-in 的 CI 作业，只编译 tiles 的 CUDA 目标 | 一条腿的时间 | 只保编译覆盖，不跑设备测试；便宜 |
| （c）现状 | 0 | 设备路径只在 88 主机上靠人工闸门覆盖；CI 永远测不到它 |

## 建议

（b）先做：它把“无头文件构建”与“有头文件构建”的差别钉在 CI 上，成本最小；要不要升级到（a）取决于
负责人愿不愿意为覆盖率付 runner 分钟。无论选哪个，**本车道加的前置条件都要留着**：主机上的构建同样
可能没有 CUDA 头（`docs/setup.*.md` 里 CUDA 是可选的）。

## 本车道做了什么

没有改 CI 的依赖安装。加了一个按构建能力跳过的前置条件，并在
`test/tools/have-cuda-build.mjs` 里写清它问的是构建、不是机器。