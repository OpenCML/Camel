# E9 的六个数，按 seed 逐条二分：每个都有程序、有最小输入、有「错在哪一步」

作者 rework（`04ee677e7`「rework: bisect the six by seed」）要的不是第四份读数，是这六个各自的**为什么**。
本页交的是六条可执行的定位：`131 141 417 484 311 419`，每一个都缩到了 10–23 行、5/5 复现，并写清是哪一次重写把
那条效应弄丢/挪走的。六条**都不是**「判据错了」：模型（`model.py`）和设计原则（效应按链执行、语句要跑）要的行为一致，
错的是覆盖之后那张图。

## 一、量法与 pin

```
树    D:\Projects\Camel-wt\e9bisect，分支 lane-e9bisect
sha   d63e84d397（= 建树时的 origin/develop）；量的时候 rev-list --count origin/develop..lane-e9bisect = 0，
      写完本页 = 1（本页唯一这一笔，`git diff --numstat HEAD~1 HEAD` 是 `237 0` 这一行）
      host /home/zjwei/camel-work/e9bisect，git HEAD d63e84d39，status 0 行，
      out/latest/build-info.json = d63e84d397 (clean) / 1341 source files / fingerprint 0a244f6e8c6f
构建  [1/726] Building CXX object modules/tensor/CMakeFiles/tensor_cmo_objects.dir/Release/kernels/gradient_generic.cpp.o
      （BUILD_EXIT=0；ccache 4.5.1 在宿主上，整棵 6.1 s）
量法  每个 seed 单独一份 full matrix：e9diff.py run --matrix full --start N --n 1 --out /tmp/e9s/N
      六份的错格 48 / 48 / 56 / 48 / 45 / 48，首次出错的遍 +fold / +fold / +inline / +fold / +cse / +fold
      —— 与 reports/2026-11-18-e9-readings-at-9d589d2f3.cn.md 的那一页一格不差
缩    e9diff.py analyze + reduce --max-groups 1，每个 seed 一份 rc00_seedN.cml，复现 5/5
```

## 二、六个的定位（每条一行）

| seed | 首次出错的遍 | 错格 | 最小输入 | 症状 | 触发它的那一次重写 |
|---|---|---|---|---|---|
| 131 | `+fold`（p05） | 48/99 | **23 行** | 闭包里的 `println('')` **挪到了输出末尾**（后打印的先出） | fold 把循环体里 `:str/format<[P]>`（**结构体数组**）折成常量 |
| 141 | `+fold` | 48/99 | **13 行** | 被调用函数的 `println` **不再运行**（一条都没打印） | fold 把实参里的 `if false then -1 else 10` 折成 `10` |
| 311 | `+inline`（p04） | 56/99 | **20 行** | 调用**之前**那条 `println('MAIN')` **不再运行** | inline 在实参处展开宏调用 `f1(true, 10.0)` |
| 417 | `+fold` | 48/99 | **10 行** | 被内联进来的 `println('X')` **不再运行** | 先把 `if true then true else false` 折掉，同一遍里又把紧随其后的 `:str/format` 折成常量 |
| 419 | `+cse`（p06） | 45/99（含 `std::cpp`） | **12 行** | `q21` 的 `println` **不再运行** | cse 把同一个 nullable 上的两次判空并成一处（`:op/tag_is_null` 折成常量 `false`） |
| 484 | `+fold` | 48/99 | **11 行** | `q27` 的 `println` **不再运行** | fold 同时折掉 `true && true`（JOIN/BRCH 消失）与 `concat([false], [false])` 的 `:struct/concat_arr` |

**症状分两类，共因是同一个形状**：重写替掉一个节点时，**链尾（sync/ctrl）跟着替身走，而只靠 ORDER 挂在链上的那条
语句就失去了它的位置**——要么掉出可达集（141/311/417/419/484：不再运行），要么被接到末尾（131：顺序改变）。
131 是唯一「挪而不丢」的，因为那条语句在循环体里，块值把它接回去了。这是六条**读数**共同的形状，不是我对代码的断言：
能把责任落到哪一行，见第五节。

## 二之二、六条各自的「错在哪一步」，一句话

- **131**：fold 折掉循环体里 `:str/format<[P]>` 之后，闭包的 `println` 从「调用处」掉到输出末尾 ⇒ 顺序错，值不错。
- **141**：fold 把实参 `if false then -1 else 10` 折成 `10` 之后，`q37` 的 `println` 不再运行。
- **311**：inline 在实参处展开宏 `f1(true, 10.0)` 之后，调用**之前**那条 `println('MAIN')` 不再运行。
- **417**：inline 把 `q31` 的体搬进 `main`；同一遍 fold 里「折掉常量 `if`」和「把 `:str/format` 折成常量」都发生，
  被搬进来的 `println` 于是不再运行（任一处单独发生都不丢）。
- **419**：cse 把 `o44 != null` 与 `if o44 == null` 的两次判空并成一处之后，`q21` 的 `println` 不再运行。
- **484**：fold 同时折掉 `true && true` 与 `concat([false], [false])` 之后，`q27` 的 `println` 不再运行。

## 三、六条探针（每条都只改一处，证明触发它的就是上面那一句）

```
141  q37(if false then -1 else 10, false)  → 空输出      ／ q37(10, false)                → 打印
     把调用结果用起来（return if q37(…) then …）仍然丢     ⇒ 不是「结果没人用」
311  q18(f1(true, 10.0), true)             → 只剩 CALLEE ／ q18(1, true)                   → MAIN、CALLEE 都在
417  q31(true) + 后面一条 format           → 两条都在    ／ if true… + 后面一条字面量       → 两条都在
     if true… + 后面一条 format             → X 丢        ⇒ 两处折掉**同时**发生才丢
419  只留 let n50 = o44 != null            → 打印        ／ 只留 if o44 == null            → 打印
     两个都留                               → 丢          ⇒ cse 把两处并成一处才丢
484  只留 true && true                     → 打印        ／ 只留 concat(…) if false        → 打印
     两个都留                               → 丢
131  闭包打 'CALLEE'（保留数组 format）    → 顺序对      ／ 闭包打 ''（换成标量 format）    → 顺序对
     闭包打 '' + 数组 format                → 顺序错      ⇒ 要「空串 + 结构体数组 format」这一对
```

## 四、六条判例草稿（**没有提交**，只在这里；不用进 `ast_hashes`，也不改任何 `.cml`）

每条都是「**不打 pass 是对的，打 pass 是错的**」——即 fast 层 `without_the_pass` / `opt` 两格要的东西，
而这里第一条格（无 pass）本身不绿，所以它们现在只能当 `std::opt::<name>` 的单格判例，不能直接 collect。

### 131（23 行，`std::opt::devirtualize std::opt::inline std::opt::fold`）

```cml
type P = { x: int, y: int }

func operator <(a: P, b: P): bool {
    return false;
}

func main(): int sync {
    let n37 = '';
    let h41 = (p39: int): int => sync {
        println('');
        return 0;
    };
    let n46 = loop s42 = ({ x: 0, y: 0 } < { x: 0, y: 0 }), c43 = (match n37 {
        case 'ab' => 1.0
        case 'k9' => 0.25
        case _ => 0.25
    }) for q44 in [false] do sync {
        let n45 = h41(int(c43));
        println('L5 {}'->format<[{ x: 0, y: 0 }]>);
        return (false, 1.0);
    };
    return 0;
}
```
期望 `<空行>` `L5 [{ x: 0, y: 0 }]`；实测 `L5 [{ x: 0, y: 0 }]` `<空行>`（8/8 稳定）。

### 141（13 行，`std::opt::inline std::opt::fold`）

```cml
func q37(a38: int, a39: bool): bool sync {
    println('');
    return false;
}

func main(): int sync {
    func g56(a57: bool): string sync {
        q37(if false then -1 else 10, false);
        return 'r:{},'->format<2>;
    };
    let n61 = g56(true);
    return 0;
}
```
期望 1 行 `<空行>`；实测 0 行。

### 311（20 行，`std::macro std::opt::inline`）

```cml
#macro func f1(a2: bool, a3: float): int {
    return int(1.0);
}

func q18(a19: int, a20: bool): bool sync {
    println('');
    return false;
}

with <k27: int>
func w26(a28: string, a29: string): int {
    return 0;
}

func main(): int sync {
    println('');
    q18(f1(true, 10.0), true);
    let n39 = w26<f1(true, 2.5)>;
    return 0;
}
```
期望 2 行（两条空行）；实测 1 行 —— 丢的是**调用之前**那条。

### 417（10 行，`std::opt::inline std::opt::fold`）

```cml
func q31(a32: int, a33: bool): bool sync {
    println('');
    return false;
}

func main(): int sync {
    q31(0, if true then true else false);
    println('L4 {} {}'->format<0, 0>);
    return 0;
}
```
期望 `<空行>` `L4 0 0`；实测只有 `L4 0 0`。**这两条打印换成 `'X'` / `'Y {}'->format<0>` 后仍然复现**：
那正是它和「只是常量 `if`」的区别——两条都在 `if` 折掉、`format` 又折掉时才丢。

### 419（12 行，`std::opt::inline std::opt::cse`）

```cml
func q21(a22: int, a23: bool): bool sync {
    println('');
    return false;
}

func main(): int sync {
    let o44: int? = 0;
    let n50 = o44 != null;
    q21(0, false);
    var v52 = if o44 == null then 0 else 100;
    return 0;
}
```
期望 1 行 `<空行>`；实测 0 行。`std::cpp` 也错（45 格里 9 格是 cpp）。

### 484（11 行，`std::opt::inline std::opt::fold`）

```cml
func q27(a28: int, a29: bool): bool sync {
    println('');
    return false;
}

func main(): int sync {
    let n43 = true && true;
    q27(0, false);
    let n46 = [(false, 0) for q44 in concat([false], [false]) if false];
    return 0;
}
```
期望 1 行 `<空行>`；实测 0 行。

## 五、几条能定位到「一条重写」，几条只能到区间

- **到一条重写的**：141（fold 的常量 `if`）、417（同一遍里 `if` 与 `:str/format` 两次替换）、
  311（inline 在实参处展开宏）、419（cse 的 nullable 判空归一）、484（`&&` 与空推导两处折掉）—— **5 条**。
- **只能到区间的**：131 —— 要「闭包里的 `println('')`」与「循环体里的 `:str/format<[P]>`」**同时**在，
  单独换掉任何一处都绿；我把它写在「fold 的两次替换之间」，没能再往下压到一条。
- **都没做到的**：把责任落到**一行代码**（`replace` 的链尾交接 / `pruneBranch` 的接位）——见下。
- 共同的那一处（值得下一笔在那里查，但本页没有证据把它写成结论）：
  `src/runtime/draft.cpp` 的 `prepareChainPlace`（1442/1615 行）与 `src/passes/opt/generic/generic.cpp`
  的替换路径——「替身拿到链尾，被 ORDER 挂着的语句掉出可达集」。这六个都是它的不同入口。

## 六、六计划读数（同一棵树、同一个 sha，`-NoBuild`）

```
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path D:\Projects\Camel-wt\e9bisect `
     -Plan test/plans/feat/compile,test/plans/feat/linear,test/plans/feat/opt,`
           test/plans/feat/para,test/plans/feat/parse,test/plans/feat/tools -Tier fast -Jobs 16 -NoBuild

feat/compile  2627 / 2610 / 0 fail / 17 known
feat/linear    861 /  856 / 0 fail /  3 known / 2 skip
feat/opt      1538 / 1538 / 0 fail /  0
feat/para      104 /  104 / 0
feat/parse     121 /  121 / 0
feat/tools      70 /   70 / 0
[SUMMARY] total: 5321, pass: 5299, fail: 0, skip: 2, known failures: 20     RUN_EXIT=0
```

六个数在 `d63e84d39` 上**一个没转对**（本页第二节的六份单 seed 全矩阵就是读数）。整份 500 seed 的
计数器本页**没有重跑**，见下。

## 七、没做 / 停手的地方

- **没有改一行源码、断言、`known_fail`、`baseline.json`、计划文件**；没有 push、没有 merge、没有 stash。
  本页是这条分支上唯一的改动（`git diff --numstat` 一页一行）。
- **没有加 `.cml` 进树**：六条判例只在本页里（因此 `test/plans/feat/parse/golden/ast_hashes.txt` 不动）。
- **没有重跑 500 seed 的整份 e9**：本页的六份是 `--start N --n 1` 的单 seed 全矩阵跑；整份计数器仍是
  `9d589d2f3` 那一页的 12/3/3（含这六个）。要新计数请跑 `-Check e9-full`。
- gpu 层同一次 gate 里跑了，`15 red / 15 recorded / 0 new`，report-only，不决定退出码（与 11-18 那页一致）。
- 宿主不是安静机器：plan 那段 `1m load 19.91`（gate 自己打的 `> 8` 警告），六计划的**个数**不受影响，
  但秒数不要与安静机器的读数比。
- **判据本身没有错**：六条都不是「用例要的行为不符合现行裁定」，所以没有触发「停手报我」。
> **更正（2026-11-18，由协调侧核出）**：本页初稿在第 13 行写的是 `236 0`，实际是 `237 0`。
> 原因不是笔误：**写这一行的时候，这一行本身还没被写进去。** 任何「这一笔有多少行」的说法，如果它就在那一笔里面，它数的总是**比最终少一行** —— 它数的是自己还没有的最后一行。
> 今晚同类事故共三次（`src/compile/gct/` 里 `halt` 的注释行数记成 4、实为 5；本页；以及一次派工提示里的数）。
> **⇒ 一个在自己的产物里数产物大小的数，必须由产物之外的人量。**
