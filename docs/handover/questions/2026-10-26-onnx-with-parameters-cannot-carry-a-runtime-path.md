# `onnx` 接口迁移撞上一个能力问题：`with` 参数是编译期常量，而调用点要运行期决定路径

（`onnx` 接入这一批**先停**，等你裁；我们不改任何 `onnx` 文件。）

## 稿件的主张与现实的冲突

稿件把 `out`／`dynamic_axes`／`target` 从**位置参数**改成**带默认值的 `with` 参数**。但 `with` 参数是**编译期常量**，于是今天靠**运行期**决定输出路径／动轴的调用点都变成非法——而它们正是那些用例的**全部意义**：

```
export_minimal.cml:19:      export_model(f, x, get_env('ONNX_OUT'))
export_capability.cml:21,23: export_model(counts, x, get_env('ONNX_OUT_UNCHECKED'), [], 'none')
                             export_model(counts, x, get_env('ONNX_OUT'))
ort_models.cml:37,40:        export_model(gate, x, out)
                             export_model((t: f32[..]): f32[..] => forward(t, w), x, out, [0])
```

（`out = get_env('ONNX_OUT')`，由 `ort_run.py` 按 `MODEL`／`ONNX_OUT` 选模型与路径。）另有**变量形式**的 `dynamic_axes` 调用点。

## 稿件里两句与实测不符的话

- 稿件 `onnx.notes.md:32` 写"机械替换，一条脚本即可"——对上面这几处**不成立**：调用点实测分布在 **172 个文件**（不是约 92 个）。
- `refusals` 那 **7 个**用例要从"故障"改成"错误值"，这是**测试语义的改写**，不是替换。

## 请选一个

1. **`out`／`dynamic_axes` 仍是普通参数**（与稿件的 `with` 设计相反）——我们就按普通参数接入；
2. **承认"运行期决定输出路径"这个能力消失**——请给出这些用例该怎么写（我们不自己重设计）。

## 另两件小事（顺带，不阻塞）

- `stdlib/process.cml` 里没有 `resource process`（资源仍在 C++ 侧）⇒ 给 `process.cml` 加 `export resource process`（`tiles.cml` 同理 `driver`），加完我们把 `test_support` 里那一行换成 `import { process } from process`；在那之前我们保留本模块声明＋规则注释。
- `docs/design/effects/plan.cn.md:48` 仍写资源声明"缺（语言层）…parser 无 `resource` 记号"，已过时（我们只报不改）。