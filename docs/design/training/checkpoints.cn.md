# 原生训练状态检查点

接口遵循训练设计稿 8.2：`nn::save_tree(dir, state)`、`nn::load_tree::<State>(dir): State!`。载入目标也可由直接的期望类型推出，如 `let state: State! = load_tree(dir)`。类型参数在编译期解析，运行时没有类型值参数。

原生库的服务器验收已通过，尚未接替 DDPM 的旧检查点入口。

结构体按字段声明顺序、普通元组按下标递归。当前叶子支持主机 `Tensor<f32/int/bool>`、`f32/float/i32/int/bool/string/Bytes`；设备值须先显式下载。数组、映射、可空或错误包装、函数和外部对象不支持。`Key` 待无状态随机数阶段实现后接入。

每个张量写精确存储 dtype 的 NPY，Bytes 写 `.bin`，其余写 `.txt`，文件名来自树路径。浮点标量使用固定长度的 IEEE 位模式十六进制文本，避免十进制解析丢失 NaN 载荷和负零；整数用十进制，布尔为 `true/false`，文本须为 UTF-8。根叶子使用 `_root` 文件名。

`manifest.json` 记录 schema、Camel 包版本、树结构、各叶子的文件名、字节数、SHA-256 和张量 dtype/shape。`save_tree(dir, state, dataset_manifest_sha256)` 可记录数据清单版本；`load_tree::<State>(dir, expected_dataset_manifest_sha256)` 不匹配时按设计稿警告，实际训练入口仍须执行自己的数据版本准入规则。

保存先把值复制成原生持有的字节，不在文件 IO 期间保留 Camel 堆的裸指针。所有文件写入同一父目录下的独占临时目录并刷盘，再以不覆盖已有目标的原子目录改名发布。Linux 使用 `renameat2(RENAME_NOREPLACE)`，Windows 使用不带替换标记的目录移动；缺少对应原语的平台明确失败。已有检查点不可覆盖。异常仅清理本次创建的文件，不递归删除其他目录。

载入先读入原生字节，检查完整结构、各文件长度/校验和、NPY 存储格式及目标类型的 dtype/已知形状；全部通过后才重建 Camel 对象。未知维度允许从清单取得实际尺寸。失败返回带路径的 `CheckpointError`，不向调用方发布半棵状态树。原生调用通过 `ArgsView::resultType()` 接收单态调用的静态结果布局；这改变了 C++ SDK 布局，模块 ABI 升至 3，核心与模块必须一起重建。

验收要求包括：各调度器往返一致、分代 GC 压力、IEEE/大整数精确性、损坏/截断/类型与形状错误、已有目录保持不变、连续更新与跨进程恢复逐位一致。完整训练的随机恢复验收仍依赖后续 `Key` 和设备常驻训练步，不能由此处的小规模优化器状态样例代替。

## 实测（2026-10-19）

88 上使用标准构建安装独立运行时 `.camel-checkpoint-v2`。先观察 CML 的命令行输出，再补 `checkpoint_verify.mjs` 和对应计划；未跑全量测试。NVM、FVM、JIT、并行调度器的保存与显式/期望类型载入一致；NVM/FVM/JIT 的分代搬移与安全点压力验证通过。10 次连续更新与 3 次更新后保存、退出、重启再更新 7 次的参数、Adam 两组动量、EMA、步数、种子和游标完全一致。大于 2^53 的整数、浮点负零、NaN 载荷、无穷、次正规数、空张量、Bytes 和中文文本均保留。

损坏的叶子校验和、dtype、已知形状、缺失清单、校验和正确但存储 dtype 不支持的 NPY，以及覆盖已有目录均被拒绝。原有自动微分/SGD 示例仍得到 `1.0 -> 0.9215999841690063` 的损失，二进制张量往返用例通过。

另在 64 MiB 张量写入临时目录后强制 `SIGKILL`：只留下 `.pending-...`，正式目标目录不存在，没有发布半个检查点。该手动探针为 `checkpoint_interrupted.cml`，不放进有时序依赖的默认 CI 检查。

服务器 v2 源码归档 SHA-256 为 `eeee7fc82a381f4ea467d5fba3f109814f570217438b69f9f3e6b871e3bc5d38`；独立运行时的 `bin/camel` 为 `006552d51a0054cc3cce00e48e3e09bc20cd5f69c90d8974bbc995af91198451`，`stdlib/nn.cmo` 为 `4997942f0beb11965be75dc8654f1f8d7cf740fb6ba084ce6567e575ce1725ef`。这些是提交前源码快照的产物标识，不等同于随后格式化提交的逐文件哈希。

Windows 构建发现 nlohmann JSON 与 `std::string_view` 的比较存在重载歧义，改用明确的 `std::string`。仅同步这一源文件并在 88 增量标准构建为 `.camel-checkpoint-v3`，上述检查点检查再次通过；没有重传全部头文件触发整套编译。v3 的 `bin/camel` SHA-256 为 `1e6b26749895dbf4ce35a9b7de298ba29e5a85247554b19383474d7e5e82f17f`，`stdlib/nn.cmo` 为 `47bd88e85104c7af178cd6e1fc0ffba7c1d58dc723f8a98d3859dee72242f263`。

按用户要求在 Windows 以 `CMAKE_BUILD_PARALLEL_LEVEL=2 npm run build` 完成同步构建，使用 OpenCML-Ext 自己的同步脚本更新完整运行时（286 个文件）。该运行时对 DDPM 的 `mnist.cml`、`train_vae.cml` 做真实 LSP 文档诊断均为零错误，新增 `bytes.cmo` 的哈希与构建目录一致；只做这些导入检查和一个 16 字节张量检查点往返，没有运行大规模测试或本机模型训练。
