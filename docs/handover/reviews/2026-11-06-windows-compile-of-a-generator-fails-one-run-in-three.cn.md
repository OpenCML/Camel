# Windows 上编译一个带捕获的生成器，三次里失败一次

**2026-11-06 22:5x 由发版侧在 Windows 上复现，develop `06edc92cb`，Release 构建（`npm run build`）。**

## 症状

同一个程序、同一条命令，几次运行里有一次直接退出，`exit=3`，stdout 为空，stderr 是：

```
Camel Invariant Violation:
    Location   : src/compile/gir/captures.cpp(63)
    Function   : node_handle_t camel::compile::gir::Builder::captureInto(
                     const compile_graph_ptr_t &, node_handle_t, const std::string &)
    Guarantee  : graph->outerGraph() != nullptr
    Reading    : A captured value must belong to a graph that encloses the capturing one.
```

栈是 Release 下不可用的（`camel::compile::gct::Tree::print` + 偏移）。

## 复现

```
out\latest\bin\camel.exe test\cases\compile\stream\generators_shapes_consumed.cml std::nvm
```

- `std::nvm` 六次：3、0、3、0、0、0 —— **两次失败，同一处位置**。
- `std::prl` 十二次：**三次失败**，同一处位置。
- 成功的那些运行，输出与 `expect_stdout_exact` 一致（`1384 / 102101112202122 / 88`）。

## 已经排除的

- **不是后端专属**：`nvm`、`fvm`、`jit`、`prl` 都会失败，位置相同，所以不是调度器或并行那块的选择。
- **不是这台机器独有的偶发**：CI 的 win-x64 腿在 `42230708125` 上正是这条红（`compile.stream / stream.generators_shapes_consumed.prl`，`exit 3`、stdout 空），而同一轮 linux-x64 只有 `gpu_scope` 一条红——**只有 Windows 红**。
- **不是时间口径、不是平台既有红**：这一条之前不在 win 的红名单里（`v0.2.44` 那一轮 win 是 14 条，逐条比对没有它）。

## 我的判断（留给你验）

`captureInto` 问的是「被捕获的节点所在的图有没有包住捕获方的那张图」，这条保证在同一次运行内不该时真时假。**时真时假说明它读到的那个指针不是程序结构的属性**——要么是未初始化的字段，要么是同一份数据被两条路径写。

时间上最近动过对象生命周期的只有 `2d0afa485` 那条 lane（帧池哨兵是 Frame、哨兵不分配不析构、裸存储当对象读必须有活对象）。**我没有把它当结论**，但它是 `v0.2.44` 之后唯一碰这块的一批，值得先看。

## 我需要你做的

1. 在 `develop` 的 head 上复现并按你的判据修（**只改 `develop`**，不要动 `release` / `main`）。
2. 修完把 sha 回我；如果这一条在你那边判定是「用例本身越界、程序该被拒绝」，也请直说并给出依据——那这个用例的写法要一起改。
3. **这一条要在 `0.2.45` 之前落地**：`v0.2.44` 这个 tag 的树里已经有它（Windows 用户编译带捕获的生成器时，三次里有一次拿不到结果）。

## 附：这一轮闸门的其它读数（供你对照）

- linux-x64：1 红（`modules.tiles / gpu_scope`），是已知的那条真缺陷；
- linux-x64-nojit：2 红（`gpu_scope` 与 `trans.cpp / narrow_reductions_four_backends`）；
- win-x64：15 红，其中 14 条是既有的平台族，**新增的就是上面这条**；
- macos-arm64：15 红，全是既有平台族。
