# 问：一条豁免该不该连「它记的那条原因」一起校验——原因不再成立时，它还是豁免吗？

`2026-11-18`｜车道 `lane-gpu`｜基线 `origin/develop = bdf5e0adb`｜引 `522380083`（gpu 读数开始对着记录棘轮）、
`c272303e3`（上一笔只删了一个名字，并写下另外十五条为什么不能删）、`075a2bddf` + `eccc6824a`（这十五条红的原因换掉了）

## 一、为什么必须问

`internal/tools/gpu-plans.exceptions.txt` 记着 **15** 个名字，文件头把其中 **13** 个归给**同一个缺陷**
（`TILES_OUTLINED 5` 而记下来的输出是 `TILES_OUTLINED 10`，四个后端一致），另外 2 个归给「比特保持」。
文件头还写死了一条规矩：**「A NAME HERE IS A KNOWN RED」**，并且「delete a name as soon as the case is fixed」。

`bdf5e0adb` 上同一条读数（`check=gpu-plans total=146 pass=114 fail=32 skip=0`）：

```
15 个记录里  13 个（outlined_execution.{4} / outlined_resident_execution.{4} /
              preferred_resident_execution.{4} / resident_graph_transfers）
              红的原因**已经不是**它记的那一条：它们现在死在
              'tiles::compile' committed a graph that reaches past the effect its own type declares
              （PassEffectBeyondType 0x040001B0），而不是 outlined 的计数
 2 个（static_loop_device / multi_state_loop_device）确实还是它记的那条（比特保持断言）
```

**⇒ 于是一张「已知红」的表，在红的原因整个换掉之后，仍然把这 13 条当成「已知、决定了什么」。
一个工具坏了会返回一个正常-looking 的结果：这里返回的是「一切照旧」。**

## 二、同一个缺陷的另一半：它同时谎报可删

`check=` 行把红名单**截断到 20 个**（`internal/tools/camel-remote.ps1:1480`），剩下的只报个数
（`red_more=32-20=12`，`:1481`）。而本地的「可删」判据只用这 20 个名字去减记录（`:2269`）：

```
$gpuGone = @($gpuBaseline | Where-Object { $gpuReds -notcontains $_ })
```

**⇒ 记录里任何一个名字，只要不在「前 20 个」里，就被当成「不红了、可以删」。** 本次跑出来的是：

```
[camel-remote] gpu-plans: no longer red, and accepted as it is:
  outlined_resident_execution.jit, outlined_resident_execution.prl,
  preferred_resident_execution.fvm, preferred_resident_execution.jit,
  preferred_resident_execution.nvm, preferred_resident_execution.prl,
  resident_graph_transfers -- internal/tools/gpu-plans.exceptions.txt can be trimmed
```

**这 7 个当次全都在红名单里**（`[FAIL]` 逐条可查）。**⇒ 同一次运行，它做了两件相反的事：
把 13 条真红说成已接受，把 7 条真红说成可删。** 而 `$gpuIncomplete`（`:2273`）在本地已经算出来了，
只是**「可删」这一支没有问它**。

## 三、三条候选路与各自的代价

```
(a) **按名字**（现状）。代价：原因换掉不会被看见；红从 A 变成 B，表仍说「已知」。
    今天的 13 条就是代价，而它下一次还会发生（换一个缺陷、同一批用例）。
(b) **名字 + 一条可机检的原因签名**，三态报出：仍是那条原因红 / 换了一条原因红（**是新闻，不是豁免**） /
    已经不红（可删）。代价：文件格式要扩一列，每条要写一个**稳定**的签名——诊断码
    （`0x040001B0`）、或用例自己打印的标记（`TILES_OUTLINED`）、或 `[INFO]` 行的固定前缀；
    签名写成一段会随措辞漂的英文，就会变成噪音。**这一条要求签名挑得稳，而稳的签名是语言自己的
    （诊断码），不是失败文本。**
(c) **名字 + 人读的注**（现状已经是「按组写一段散文」）。代价：机械上什么都不变，也就是 (a)。
```

## 四、默认建议（写死）

```
**按 (b) 走，并且把「可删」判据改成数全量：**

1. 豁免必须连同「它记的那条原因」一起校验。原因不再成立时，那一条**不再是豁免**——
   与一条新红同等地报出来（「记的是 outlined 计数，现在拒的是 0x040001B0」）。
2. `check=` 行**不许截断**；若要截断，被截掉的必须是**名字之外的东西**，而不许让「可删」
   判据只看见前 20 个。最省事的写法：行里给全量（32 个名字约 1.3 KB，一行扫得动），
   `red_more` 恒为 0；真要为扫描性保留 20 个上限，就必须**另起一行给全量**，
   并且 `$gpuGone` 只在 `red_more == 0` 时才算。
```

**⇒ 理由是这份文件自己的规矩**：它写着「一个 name 是 KNOWN RED」，而「已知」是一个**关于原因的断言**，
不是关于名字的断言。只比名字，等于把一个断言降级成一次字符串匹配——而这正是今晚那句
「一个工具坏了会返回一个正常-looking 的结果」的最完整形态。

## 五、落点（改之前先报，等放行）

```
必改两处：
  internal/tools/camel-remote.ps1:1475-1481   host 侧截断与 red_more（要么给全量，要么另起一行给全量）
  internal/tools/camel-remote.ps1:2269,2288   「可删」判据与那句 can be trimmed（必须问 redMore，
                                              incomplete 时不许算 gone）
格式扩展（(b) 的另一半，改之前要作者点头）：
  internal/tools/camel-remote.ps1:2247-2252   读记录（改成 名字 + 原因签名）
  internal/tools/camel-remote.ps1:2260-2274   比较：三态（同原因红 / 换因红 / 已不红）
  internal/tools/gpu-plans.exceptions.txt     15 条各补一条签名（**本车道一个字没动**）
```

## 六、这一条与那 32 条的关系

**无关，且必须分开修。** 32 条里 30 条是 `tiles` 遍先发体后补型（见
`2026-11-18-the-tiles-pass-publishes-a-body-before-its-type.cn.md`），2 条是这张表里两条比特保持老红。
而这一条说的是**表本身**：它让「13 条换了原因」不可见，又让「7 条还在红」看起来可删。
把那 30 条修好之后，这张表会同时暴露第三件事——**`outlined_execution.*` 等 13 条按名字算「已不红」，
按原因算「从来没被修过」**，两种读法给出的结论相反，而只有 (b) 能说出它。