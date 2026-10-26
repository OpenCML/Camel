# 读捕获 cell 的闭包：类型说 `pure`，运行时足迹说 `rmw`

（一处两套读数互相矛盾的地方，修法牵涉一个取舍，交你定。）

## 现象（逐字）

```cml
var c = 0;
let bump = sync () => { c = c + 1; return c };
return bump()
```

放进 `func run(): int pure` 里：

- **编译器的类型**：`exit=0`（无诊断），闭包的类型打印成 `sync () => int pure`；
- **运行时的足迹**（同一编译器、同一个闭包）：
  `RESOURCES footprint __g_125__ rmw variable capture 0 whole; result fresh`。

两处读数互相矛盾：运行时按捕获**命名了那个 cell**，编译器的度量**什么都没记**。

## 机理

`footprintOf` 的 `Capture` 分支 `return false` ⇒ **整具身体不可测量** ⇒ 不发布资源集 ⇒ 类型留着 `FILL` 的空 `Concrete` 签名 ⇒ 打印成 `pure`。

## 试过的改法与它撞到的取舍（已回退，未落地）

把捕获**按位置**记进度量（`captures.push_back(access.binding.index)`，并把捕获端口编号进 `BodyReach`）：

- 闭包类型变成 `sync () => int uses capture 0` ✓，`run` 的 `pure` 在 `10:1` 被拒（`UsesPromiseViolated`）✓；
- **但** `Resolver::fromSignature` 对签名里的 captures 只能保守读——`ResourceSignature::captures` **只存位置，没有类别/模式**（现在那里的循环逐字是 `for ([[maybe_unused]] uint8_t capture : signature.captures)`）⇒ "经由闭包值的调用"从"命名到那个 cell"退化成 `rmw unknown (any); conservative`；
- 结果：resources 计划里你特意钉住精度的断言 `the cells of c and d are different resources` 立刻变红 ✗。

## 请裁

`ResourceSignature::captures` 里要不要放**类别与模式**（这样"经由闭包值的调用"仍能精确指名到某个 cell）？
还是闭包类型本来就只该说一件更粗的事（那 `pure` 这个打印必须至少不再是 `pure`），精度由别处保证？
在你定之前我们**不动**这块（回退干净）。