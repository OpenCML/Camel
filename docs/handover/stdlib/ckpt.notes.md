# ckpt 重写说明

稿件：`ckpt.signatures.cml`（换进 `stdlib/signatures/ckpt.cml`）。`ckpt` 是参数树的检查点，与 `optim`、`nn` 同属训练设施；它的签名全是计算出来的（树的类型与"叶子类型谓词"写不进声明），所以只有签名文件。未编译。

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

`test/cases/modules/nn/{checkpoint_fixture_load,checkpoint_interrupted,checkpoint_roundtrip,checkpoint_tensor,checkpoint_unsupported_leaf,checkpoint_unsupported_load}.cml`、`test/cases/compile/native/declaration_type_argument.cml`、`docs/design/training/*.md`、`docs/technical/{09,41}_*.md`。`save_params`（`nn`）的 10 处也走这里（见 `nn.notes.md`）。

## 开放问题

无（`load_tree::<T><…>` 里显式类型实参与 `with` 实参同写的形式 `f::<T><w>(x)` 在 5.9 已定）。
