# `tiles::compile` 的失败要变成"遍的错误值"，但核心没有"遍返回错误值"这个机制

（`tiles` 接入已完成并整层绿；这一子项按稿件做不到，请你裁。）

## 稿件要求

`docs/handover/stdlib/tiles.cml` 要求 `tiles::compile` 的失败**由诊断改成遍的错误值**（`GpuScopeUnsupported`）。

## 实测：核心没有这条路

- `makeError` 今天**只服务算子**（算子可以交出错误值）；
- `src/execute/pass/**` 里**没有任何遍返回错误值**的机制，遍的失败一律走诊断；
- 也就是说这不是 `tiles` 一个模块的事，而是**遍 API 的缺口**。

## 我们的现状（绿）

保持现状＝**诊断**；用例 `gpu_scope.unsupported_operator_is_a_compile_error` 仍绿。其余 `tiles` 接入已按稿件落地（`export resource driver`、三个声明 `uses driver`、删 `module tiles` 行、删 `@effect`、`#cpu` 按稿件"实现前是目标不是现状"只留 `#gpu`；`device(): string!` 的失败改成错误值 `DeviceUnavailable`，加 `rtdata/tagged.h`；`--devices` 点名不存在的卡仍是**启动期故障**不动）。

## 请选一个

1. **遍 API 该长出错误值**（那么稿件的写法是对的，我们在遍 API 这一层实现，`tiles` 跟着改）；
2. **遍的失败仍然走诊断**（那么请把稿件里那一行改掉，我们据此把用例的措辞钉牢）。