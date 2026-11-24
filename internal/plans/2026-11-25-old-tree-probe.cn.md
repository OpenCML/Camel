# 旧树探针：把「工具比树新」变成一件可复现、可断言的事

日期 2026-11-25。起因见 `internal/backlog.md` 的 `C-1124-157` 与 `C-1124-161`，教训见 `C-1124-164`。

## 1. 这件事的形状

宿主侧的 `camel-run` 与树里的 `test/tools/run-tests.mjs` 是两件会各自演化的东西。调用方比树新时，它会传一个树不认识的词（`--list-cases` 在 `94e3e0f3f9` 之前就不存在）。今天树对这个词的反应是：

```
未知词被 options.targets.push(path.resolve(cwd, arg)) 吃成 target
  ⇒ walkPlans 的 fs.statSync ENOENT 抛栈
  ⇒ 消费者包成 "the plan loader refused this selection (exit 1): …"
  ⇒ 闸门 exit 2，打 "[ERROR] --changed: the cases this change selects could not be computed"
```

**结果是三个错因共用一个出口**：真的选不到、算不出来、工具版本错配，人会读，只读退出码的程序读不出来。而今晚这条缺陷**被两个席位给出了两个不同且都不对的解释**，因为日志里没有「哪棵树、哪个词、哪一天」这三样东西。

## 2. 工具要做的事

`scripts/old-tree-probe.mjs`（名字待实现者定，改名前先说一声，页名跟着改）。

### 2.1 `--build <ref>`

在这个根下（专用根，带清单）拼一棵「比调用方旧」的树：

```
git archive <ref> -- test/tools test/runner.mjs test/plan-selection.json scripts/selection-cases.mjs
  ⇒ 解到 <根>/<name>/，git init + 一笔提交，把 origin/develop 指过去
```

**取不到 `ref` 时不许静默退回合成。**退回时必须：

- 第一行打印 `[PROBE] built by synthesis, not by archive: <ref> could not be read`；
- `--check` 默认**只接受 archive 建的树**，要跑合成树必须显式 `--allow-synthesis`。

理由：「合成树的读数」与「旧真树的读数」在日志里长得一样，而今晚两次错解释就是这个形状。

### 2.2 `--check`

用**真的** `scripts/selection-cases.mjs` 去列那棵树的 case，断言：

| 断言 | 内容 |
|---|---|
| 出现 | `this tree's runner does not know <word>` 与 `this is not an empty selection` |
| 不出现 | `refused this selection`、`the cases this change selects could not be computed` |
| 退出码 | `2` |
| 红绿都打印 | **那条原文**，不是只打印 OK |

### 2.3 `--self`（默认开启）

单独跑工具时，**先**对「本检出这棵树」断言「不出现 `unknown option`」，**再**对旧树断言「出现」。

顺序写死。理由：**一个只会在坑里变红的检查，和一个坏了就常红的检查，在日志里长得一样。**

### 2.4 自描述（输出三件套）

任何一次运行，输出里必须同时有：

```
[PROBE] this tree    <sha>（committed <time>）
[PROBE] older tree   <sha>（committed <time>），built by <archive|synthesis>
[PROBE] the word     <--xxx>
[PROBE] this is a reproduction of "the caller is newer than this tree", not a run reading
```

## 3. 它必须避免的坑（都踩过）

1. **不许用桩。** 桩打印的是写桩的人的字，测试就只验证他自己。`argv.gate` 能咬出那个 ANSI 缺陷，正因为它跑的是真 runner 的真输出。
2. **strip 放在读取处，不放宽正则。** 工具自己读 `[ERROR]` 行时也一样：`[ERROR]` 行首是转义序列，`^\[ERROR\]` 一次都不命中（`C-1124-164`，今晚第三次同一根因）。
3. **三个退出码分开**：`0` = 坑在且行为对；`1` = 坑在但行为不对（红）；`2` = 树没建成 / 读不到。
   **「读不到」绝不能落成 `0`。**

## 4. 完成标志

```
① `node scripts/old-tree-probe.mjs --build <旧 ref> --check` 在本检出上 exit 0，且输出三件套齐全
② 在**未修**的树上同一命令 exit 1，并打印 `refused this selection` 那句（证明它咬得住）
③ `--self` 在正常树上 exit 0，且不出现 `unknown option`
④ 合成树不显式 `--allow-synthesis` 时被拒（exit 2），拒绝原文一行
```

## 5. 不做的事

- 不改任何退出码语义（`C-1124-157` 记的那条另有派工）。
- 不实现 `--list-cases`（它在 `94e3e0f3f9` 起就有）。
- 不碰 `merge-*` 与生成页计数。