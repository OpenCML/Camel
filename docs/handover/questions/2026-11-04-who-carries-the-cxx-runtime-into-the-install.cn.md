# 安装自己带 C++ 运行时，还是继续要调用方补一条导出？

## 问什么

`camel` 与 `libcamel.so` 要的 `GLIBCXX_3.4.31..36` 比老发行版自带的 `libstdc++` 新。今天
**develop 上的答案是一条环境导出**：`docs/handover/pitfalls.cn.md` 的 GLIBCXX 那一行写的是"跑它
的环境少一条导出"，修法是"补齐闸门那几条导出（`LD_LIBRARY_PATH`、`CAMEL_HOME`、`PATH`），或者干脆
别搬出闸门"。

**`lane-rpath` 写的是相反的答案**：让安装自己带——给 `libcamel` 补 `$ORIGIN;$ORIGIN/../libs`
两条 rpath，并让 `scripts/collect-out.js` 把链接时那个驱动自己报出来的 `libstdc++.so.6` 真实文件
拷进 `libs/`。这两笔都不在 develop 上。

两条路各自自洽，**代价只有作者能权衡**（安装大小 vs. 每一个闸门外的调用方），所以写成这一页。

## 读数（可复核）

- `CMakeLists.txt` 给 `libcamel` 只写了 `CMAKE_INSTALL_RPATH "$ORIGIN"`（`#Cross-platform RPATH
  settings`），没有 `$ORIGIN/../libs`；`tools/libcamel_tool.cmake` 给**工具**写的是
  `$ORIGIN/../libs`。也就是说 `libs/` 这条 rpath 只有我们的工具继承得到，直接 `dlopen` 或
  Python 绑定的消费者没有。
- `scripts/collect-out.js#collectPythonRuntimeDlls` 只收 Python 的 DLL；没有
  `CXX_RUNTIME_LIBRARY`、没有 `findCxxRuntimeLibrary`、没有 `isSystemLibraryDir`（在 `lane-rpath`
  里才有）。`libs/` 里因此没有 `libstdc++`。
- 代价是实测的，不是推的：2026-11-03 在干净 shell 里 `env -u LD_LIBRARY_PATH` 单跑同一夹具是
  `total: 13, pass: 0, fail: 13`，stderr 第一行 `GLIBCXX_3.4.32 not found`；同一个 shell 补上导出
  就是 13/13（同上那一行的记录）。
- `lane-rpathdoc` 里"`pass: 0` 行记上 rpath 根修好、两条导出退役"这句话**在 develop 上不成立**：
  rpath 修复没进树，那一行今天仍写"少一条导出"。所以这条不是"再抄一遍"，是要先裁方向。

## 三条路，各带代价

**A. 安装自己带（`lane-rpath` 的两笔）**
- `CMakeLists.txt`：`libcamel` 加 `BUILD_RPATH`/`INSTALL_RPATH = "$ORIGIN;$ORIGIN/../libs"`（仅 `UNIX`）。
- `scripts/collect-out.js`：问链接器驱动 `--print-file-name=libstdc++.so.6`，是绝对路径就把它
  `realpath` 后拷进 `libs/`；落在 `/lib*`、`/usr/lib*` 的不拷（发行版目录不需要复制）。
- 代价：`libs/` 多约 24 MB（`libcamel` 本身 20 MB），wheel 与安装都大一圈；工具链换地方后要重新
  `collect-out` 才更新那份拷贝；`ldd` 会把 `libstdc++` 指到安装内的文件，排查时要知道这一层。
- 好处：闸门外的手搓 ssh、拷到别处单跑的夹具、Python 绑定直接加载 `libcamel.so` 都不再需要导出；
  这是 `pass: 0` 那种"看着像脚本坏了"的形状的根治。

**B. 维持今天的答案（导出）**
- 一行代码不改。
- 代价：任何没有继承那条导出的调用方都拿不到一个能启动的二进制，而且失败形状是"整批 FAIL、
  `pass: 0`"，与负载问题一模一样——已经在 pitfalls 上耗过一次。
- 好处：安装体积不变；发布物不嵌任何第三方 `.so`。

**C. 只为 playbook 加一条响亮的自检**
- 启动时若 `libstdc++` 版本不够，打一行"请导出 `LD_LIBRARY_PATH=<conda>/lib`"再退出，而不是让
  加载器直接抛 `GLIBCXX_* not found`。
- 代价：仍然要人手动补；好处是失败的形状从"整批红"变成一句话。

## 默认（本轮按这个开工，不等回复）

**走 A**，因为代价是**安装大小**这一个可量的数，而 B 的代价是**每一个闸门外调用方的可用性**，
且已经在 `docs/handover/pitfalls.cn.md` 的 GLIBCXX 行实测过 `pass: 0`。C 不排斥 A，A 落地之后
C 仍然值得做（版本不符时的一句话），但 C 单独上不解决任何一条用例。

作者若裁 B，`lane-rpath` 就该整条废弃、`lane-rpathdoc` 连同它一起；若裁 C，只取 `lane-rpathdoc`
的行文而丢掉两笔代码。