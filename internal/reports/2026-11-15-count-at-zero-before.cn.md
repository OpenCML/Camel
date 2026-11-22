# 修前读数：`ref.boxes_made_per_call_ten` 在四个后端上的十轮分布

- 日期：2026-11-15
- 车道：`lane-refcount`（工作树 `D:\Projects\Camel-wt\refcount`，基线 `df057ee8a`，先并入作者的三份答复 `4716dd04c`）
- 被测二进制：主机 `88zjwei` 的 `~/camel-work/refcount`（`[provenance]` 原文见文末），一次构建同时供两次读数
- 本车道**未改** `src/`、`test/`、期望值；这一页只记读数
- 判据：原则 10.5「不可变的值（…`Ref` 句柄与盒子）只靠计数回收，计数归零立即释放，计数必须等于持有者数」；
  作者 2026-11-15 的答复（`internal/reviews/cc-2026-11-15-can-a-flaky-cell-wear-a-known-fail-mark.cn.md` 文末）

## 为什么要一个临时计划才读得到

运行器只在**失败**时打印用例输出，绿轮的 `FOREIGN_RESOURCES` 不出现，于是「今天读的是什么」只在红轮可见——
这正是这一格「看不见」的地方。所以量的时候用一个临时计划（未提交，量完即删）：这一条用例按后端各跑 10 次，
`expect_exit = 1` 而程序退出 0，每轮都按「退出码不符」失败，于是每轮都打印它自己的那一行。旗标与
`test/plans/feat/compile/ref.plan.toml` 的那一格逐字相同：
`--gc-stress alloc=7,safepoint=3,kind=both --gc-check foreign,verify=both --gc generational`。

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\refcount `
     -Plan test/plans/feat/compile/zz-refreading.plan.toml -Jobs 1     # 再以 -Jobs 8 一次
```

两次的 `[SUMMARY]` 都是 `total: 40, pass: 0, fail: 40`，**这是量法造成的，不是缺陷**：那 40 条按设计全部失败。

## 表一 `--jobs 1`，每后端 10 轮（机器 1 分钟负载 19–20，另有车道在跑）

| 后端 | 读数分布（`created/released/live/disposed/finalized`） | 稳定？ |
|---|---|---|
| `nvm` | `11/11/0/0/11` ×10 | 是 |
| `fvm` | `11/9/2/2/9` ×10 | 是 |
| `jit` | `11/9/2/2/9` ×10 | 是 |
| `prl` | `11/11/0/0/11` ×9，`11/5/6/6/5` ×1 | **否** |

## 表二 `--jobs 8`，每后端 10 轮（1 分钟负载 18–19）

| 后端 | 读数分布 | 稳定？ |
|---|---|---|
| `nvm` | `11/11/0/0/11` ×10 | 是 |
| `fvm` | `11/9/2/2/9` ×10 | 是 |
| `jit` | `11/9/2/2/9` ×10 | 是 |
| `prl` | `11/11/0/0/11` ×10 | 这一批抽到绿 |

## 三条读数

1. **`created` 在四个后端、两个宽度、20 轮里恒为 11。** 没有盒子被漏数，也没有盒子被数两次。
2. **抖的只有 `prl`，而它这一批在 `--jobs 1` 下红、`--jobs 8` 下绿**：并行的宽窄不是它的自变量（与前一条车道的
   20 遍/40 遍结果同族）；它抖的是「压测收集落在哪个程序点」，见
   `internal/reports/2026-11-15-prl-counted-boxes-return.cn.md` §4。
3. **`fvm`/`jit` 不是对的，它们只是稳的。** 它们 20/20 轮读 `live=2`：两个盒子是最后几次调用的，
   句柄随帧没了，而之后再没有一次收集看见它们。判据 1 的验收（四后端 `11/11/0`）因此**不是把 prl 修稳**，
   而是让「归零即释放」在四个后端都成立——今天四格里没有一格是计数说了算：
   `nvm`/`prl` 读到 0 是收集恰好跑够了，`fvm`/`jit` 读到 2 是收集恰好少跑了一次。

## `[provenance]` 原文

```
[provenance] tip (dirty: 1 files) | binary built 2026-11-15T03:28:31-05:00 | tree /home/zjwei/camel-work/refcount | tier fast | jobs 1
[provenance] tip (dirty: 1 files) | binary built 2026-11-15T03:29:14-05:00 | tree /home/zjwei/camel-work/refcount | tier fast | jobs 8
```

（`dirty: 1 files` 就是上面那个临时的量测计划；它没有提交。两次读的是同一个二进制，`build 2.7 s` / `2.6 s`
是同一份源码的重建，`out/latest/build-info.json` 记的是同一个内容指纹。）