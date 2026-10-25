# `#gpu` 作用域里的主机效果：允许了，但写在作用域体内的那个不执行

## 结论（对照 `docs/design/gpu/plan.cn.md` §2.4 与 2026-10-24 晚的第三条裁定）

"主机效果是边界不是拒绝"这一半已经落地：`#gpu` 里写 `println` 不再被 `GpuScopeUnsupported` 拒绝，作用域照旧是一个设备单元（逐字读数见下）。

但**写在作用域体内的效果既不执行、也没有插入 `download`**。这次的改动只把它从拒绝名单里拿掉，没有给它一个传输：`tiles::compile` 把作用域的算子抽成段之后，效果节点仍留在图里（`tiles::compile std::jgir` 里能看到 `:io/println`、`role: effect`），但运行它没有任何输出。

## 逐字读数

程序（`test/cases/modules/tiles/gpu_scope_host_effect.cml` 的形状）：

```cml
#gpu
func observed(x: Tensor<f32, [3]>): Tensor<f32, [3]> sync {
    let y = x * 2.0
    println(sum(y))
    return y + 1.0
}

func main(): int sync {
    let x = reshape([1.0, 2.0, 3.0] as f32[], [3])
    println('result {}'->format<observed(x)>)
    return 0
}
```

1. **改动前**（`~/camel-work/verify`，develop 未改动的树）：

   ```
   error[RuntimeDiag::GpuScopeUnsupported]: `#gpu` scope 'observed': operator ':io/println' cannot run on the device (no device implementation).
   ```

2. **改动后、不带 `tiles::compile`**（`--devices cpu ... std::nvm`）：效果**会跑**，`12.0` 打印两次（未打标签的同样函数一次、`#gpu` 的一次）。

3. **改动后、带 `tiles::compile`**（`--devices cpu`、以及 `--devices 0` 真卡各一次）：不报错，`observed` 是 `body=gpu-required`；但 `observed` 自己的驻留读数 `"downloads":0`，`println` 那行**没有输出**，`main` 里的 `transfers: 1` 是 `main` 读作用域结果那一次，不是观察点那一次。

4. 对照：把 `println` 写在作用域**外面**（`main` 读作用域的返回值）时，`--devices 0` 下 `std::profile` 记出 `device kernels 3`、`transfers 4（host->device 20 B, device->host 12 B）`、`synchronisations 2`，即跨边界取值是有传输的。

## 缺的是哪一环（初步定位，未改）

`modules/tiles/src/regions.cpp` 的 `planRegionTransfers` 用"图输出/出口可达"算 `live`（`if (nodes[id].output) pending.push_back(id);`），而 `modules/tiles/include/tiles/internal/regions.h` 的注释要求调用方"把全部可观察根（含效果/控制根）标出来"。`modules/tiles/src/graph_regions.cpp` 里只标了 `graph.outputNodeRef()` 与 `graph.exitNodeRef()`，效果节点没标，于是没有任何值依赖它的效果不算 live：不插入 `download`，也不在运行的那一批里。

我按这条线索试过一版（把效果节点也标成 `output`，`graph_regions.cpp` 一处改动），`test/plans/feat/modules/tiles.plan.toml` 的读数**一点没变**（`downloads` 仍是 0、效果仍不打印），说明要么效果节点的 `kind` 不是 `Oper`（`println` 可能是 Call 锚点），要么决定执行的那一处不读这张表。改动已撤回：没验证过的东西不进树。

## 需要裁定的

1. **要不要现在修**？修法大概是"效果是根"这条要在真正决定"跑哪些节点"的那一处生效（可能不止 `planRegionTransfers`），并顺带把"没有设备时不写 `download`"（`region_outline.cpp` 里 `if (device)` 那一圈）讲清楚：`--devices cpu` 下这份传输根本不会被写出来，所以"观察点 download"这条验收只能在有卡时读。
2. **写在作用域体内的效果，语义上算不算作用域的一部分**？如果算，它必须有一个跨边界取值；如果不算（"效果一律在作用域外的宿主上"），那 `#gpu` 体里写效果就应该给一条明确的诊断，而不是静默丢掉——静默丢掉是现在最坏的行为。
3. 这次的验收只把"接受"这一半钉进了 `gpu_scope.host_effect_is_the_boundary`（以及有卡时的 `gpu_scope.host_effect_synchronises`），"体内效果的执行与传输"没有钉：钉住一个缺陷等于把它冻成期望。