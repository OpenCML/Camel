# 库的资源声明：`export resource` 不编译，`uses <非通用资源>` 要先在本模块声明

（施工方在接入 `test_support` 时撞到的两处，按本意绕开了，但绕法对不对请你定。）

## 一、`export resource X` 编译不过

逐字（`stdlib/test_support.cml:1:1`）：

```
[Error] SemanticDiag::UnresolvedReference 0x0300000B: … 'rendezvous_point' …
```

把 `export resource rendezvous_point` 退成**私有** `resource rendezvous_point` 后编译通过，且同文件里的 `uses rendezvous_point` 正常解析
（`builder.cpp:1270` 的"同模块声明的资源保留声明时的名字"）。逐字红/绿各一份在手边，需要可给。
⇒ 现在的口径是"**库的资源只能私有**"。`docs/design/effects/plan.cn.md` 自己也记着"资源由库用 `resource` 声明…缺"。

**会撞同一处**：`tiles.cml` 草稿里的 `export resource driver`。

## 二、`uses process` 报 `UnknownResource`

逐字（`stdlib/test_support.cml:25:33`，`thread_count(): int uses process`）：

```
[Error] SemanticDiag::UnknownResource 0x030001A0: The name 'process' in the resource clause of '() => int'
is neither a parameter nor a resource. … one of the resources every program has (stdout, stderr, stdin, clock, env, fs) …
```

`process` 不在"每个程序都有的资源"里，草稿也没在模块内声明它。在模块里补一句 `resource process` 后通过
（C++ 侧 `externalAmbient("process", Read)` 不变，调度语义不动）。

**会撞同一处**：`process.cml` 草稿里 `exit` / `thread_count` 的 `uses process`（`sleep`→`clock`、`user_name`/`cpu_count`→`env` 是标准资源，没问题）。

## 三、请你定三件

1. 库要怎么**导出**资源？`export resource` 是语法缺口（要补），还是资源本来就不导出、用它的模块各自私有声明？
2. `uses X` 能不能引用**别模块**声明的资源？要写限定名（`mod:res`）还是裸名？跨模块时"声明时的名字"这条规则还成立吗？
3. `process` 这类"外部环境"资源，是每个用它的模块自己 `resource` 声明，还是该由核心/该模块统一导出？

## 四、我们暂行的做法（若不对，说一声就改）

模块内**私有** `resource X`；本模块 `uses X` 写**裸名**；C++ 侧 `declareResources` 用**同名**（不一致会报
`NativeResourceMismatch`，逐字：`The declaration 'rendezvous' says 'uses rendezvous_point', but the operator
'test_support:rendezvous' declares 'uses test_support:rendezvous_point'`）。`test_support` 上已验证：探针 `104/104`、
`checks 8/8`、`RUN_EXIT=0`。