# ckpt 重写说明

稿件：`ckpt.cml`（换进 `stdlib/ckpt.cml`，删掉 `stdlib/signatures/ckpt.cml`）。`ckpt` 是参数树的检查点，与 `optim`、`nn` 同属训练设施。2026-10-27 起，类型函数（`ElementOf`）与形状词让"能存进检查点的叶子"写成 Camel 的谓词 `has_ckpt_form`，两个函数成了普通声明，不再需要签名文件。

## 改了什么

1. **树是数据，目录是配置**：`tree->save_tree<dir: 'out/ckpt'>`、`'out/ckpt'->load_tree::<T>`。原先 `save_tree(dir, tree, dataset_manifest_sha256?)`。可选的数据集清单哈希改为 `string? = null` 的 `with` 参数，两个函数都带。
2. **失败是错误值**：`save_tree` 原先抛 `FileSystemError` 故障；磁盘、权限、符号链接这类环境失败现在是 `void!`，种类 `PermissionDenied`、`IOError`。`load_tree` 本来就是 `T!`（`CheckpointError`），补上"目录不存在"= `NotFound`。
3. 删 `@effect`，签名写 `uses fs`；去掉 `tree_leaves`（它是树库的函数，`stdlib/tree.cml` 的注释已说明它暂时仍由 `ckpt:tree_leaves` 提供）。
4. 例子中的 `save_tree('out/ckpt', {…})` 改为管道写法。

## 函数表

| 项 | 处理 |
|---|---|
| `save_tree(dir, tree, hash?)` | 改：`tree->save_tree<dir, dataset_manifest_sha256>`，`void!` |
| `load_tree(dir, hash?)` | 改：`dir->load_tree::<T><dataset_manifest_sha256>`，`T!` |
| `tree_leaves` | 不在此处 |

## C++ 侧改动（`modules/ckpt`）

- 两个算子从读位置参数 `norm.get(0..2)` 改读 `with` 视图（`dir`、`dataset_manifest_sha256`）与数据；`save_tree` 失败路径 `throwRuntimeFault(FileSystemError…)` 改 `makeError`。种类映射沿用 `fs` 的 `errorKind()`。

## 调用点

`test/cases/modules/nn/{checkpoint_fixture_load,checkpoint_interrupted,checkpoint_roundtrip,checkpoint_tensor,checkpoint_unsupported_leaf,checkpoint_unsupported_load}.cml`、`docs/design/training/*.md`、`docs/technical/{09,41}_*.md`。`save_params`（`nn`）的 10 处也走这里（见 `nn.notes.md`）。

## 接入时要知道的

- 不合的树今天在 C++ 里报错并指出叶子的路径；改成 `when has_ckpt_form::<T>()` 后报的是条件不成立（`WhenConditionUnmet` 指名 `has_ckpt_form`）。条件不成立时的说明要能指出是哪个成员不满足，这是 `when` 诊断的通用改进，不为 `ckpt` 单独做。

## 开放问题

无（`load_tree::<T><…>` 里显式类型实参与 `with` 实参同写的形式 `f::<T><w>(x)` 在 5.9 已定）。
