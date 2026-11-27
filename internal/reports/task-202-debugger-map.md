# task-202 — `GCGraph` / `GraphDraft` in `src/tools`: map for the refactor

Read-only survey. No code was changed. Tree: `lane/gc-tools @ 426500675d` (clean).

## Three conclusions

1. **The one entry point is `GCGraph::view()`** — declared `src/kernel/include/camel/runtime/graph.h:1150`
   (`const camel::graph::GraphView &view() const override;`), defined
   `src/kernel/graph/gc_graph_view.cpp:41`, caching a `FrozenGraphAdapter` in `viewCache_`.
   Every `GCGraph` has exactly one adapter, its address is stable, so **identity containers can move to
   `const GraphView *` unchanged** (one adapter per graph, one graph per adapter).
2. **Per-file non-comment references**: `program_index.h` 16, `program_index.cpp` 15,
   `debug_core.h` 9, `debug_core.cpp` 7, `frames.cpp` 3, `pipeline.cpp` 1, `run.cpp` 1,
   `camel-cli/main.cpp` 2. Raw `git grep` = 54; comment-only = 2; **non-comment = 52**.
3. **The types live in the headers, so the headers come first.** Changing `frames.cpp` alone does not
   compile: its `GraphInfo::owners` element type and `ProgramIndex::graph(GCGraph *)` are declared in
   `program_index.h`, and `pipelineBoundary` in `debug_core.h`. Order: types and signatures in the two
   headers, then the `.cpp`s. `program_index.cpp:326` `traceGraphs(*g, [&](GCGraph *next) { … })` is the
   one place that may need a `GraphView` overload on the kernel side; judge it separately.

## The 52 sites

| file:line | class | head declaration | minimal cost |
|---|---|---|---|
| camel-cli/main.cpp:102 | 回调签名 | pass-event API (`camel::runtime`) | 要动签名 (shared with main.cpp:114) |
| camel-cli/main.cpp:114 | 回调签名 | pass-event API (`camel::runtime`) | 要动签名 |
| debugger/src/run.cpp:52 | 回调签名 | pass-event API (`camel::runtime`) | 要动签名 |
| core/pipeline.cpp:12 | 回调签名 | `debug_core.h:85` | 要动签名 |
| core/debug_core.h:76 | 回调签名 | self | 要动签名 |
| core/debug_core.h:85 | 回调签名 | self | 要动签名 |
| core/debug_core.h:112 | 回调签名 | pass-event API (override) | 要动签名 |
| core/debug_core.cpp:161 | 回调签名 | `debug_core.h:76` | 换类型即可 |
| core/debug_core.cpp:475 | 回调签名 | `debug_core.h:112` | 换类型即可 |
| core/program_index.cpp:326 | 回调签名 | `traceGraphs` in `camel::runtime` | **可能牵 kernel**（需 `GraphView` 版重载） |
| core/debug_core.h:106 | 身份容器 | self (`ShadowFrame::graph` param) | 换类型即可 |
| core/debug_core.h:130 | 身份容器 | self (`ShadowFrame::graph`) | 换类型即可 |
| core/debug_core.h:255 | 身份容器 | self | 换类型即可 |
| core/debug_core.h:257 | 身份容器 | self | 换类型即可 |
| core/debug_core.h:259 | 身份容器 | self | 换类型即可 |
| core/debug_core.cpp:489 | 身份容器 | `debug_core.h:255` | 换类型即可 |
| core/debug_core.cpp:507 | 身份容器 | `debug_core.h:257` | 换类型即可 |
| core/debug_core.cpp:523 | 身份容器 | `debug_core.h:259` | 换类型即可 |
| core/debug_core.cpp:542 | 身份容器 | `debug_core.h:257` (body) | 换类型即可 |
| core/frames.cpp:40 | 身份容器 | `program_index.h:121` (`owners`) | 换类型即可 |
| core/program_index.h:112 | 身份容器 | self (`GraphInfo::graph`) | 换类型即可 |
| core/program_index.h:121 | 身份容器 | self (`GraphInfo::owners`) | 换类型即可 |
| core/program_index.h:227 | 身份容器 | self (`runningGraphs`) | 换类型即可 |
| core/program_index.h:271 | 身份容器 | self (map by graph) | 换类型即可 |
| core/program_index.h:275 | 身份容器 | self (helper) | 换类型即可 |
| core/program_index.h:276 | 身份容器 | self (helper) | 换类型即可 |
| core/program_index.h:303 | 身份容器 | self (`graphs_`) | 换类型即可 |
| core/program_index.h:312 | 身份容器 | self (`initialGraphs_`) | 换类型即可 |
| core/program_index.h:313 | 身份容器 | self (`runningGraphs_`) | 换类型即可 |
| core/program_index.h:325 | 身份容器 | self (second map) | 换类型即可 |
| core/program_index.cpp:292 | 身份容器 | `program_index.h:275` | 换类型即可 |
| core/program_index.cpp:299 | 身份容器 | local `unordered_set` | 换类型即可 |
| core/program_index.cpp:300 | 身份容器 | local `deque` | 换类型即可 |
| core/program_index.cpp:391 | 身份容器 | local `unordered_set` | 换类型即可 |
| core/program_index.cpp:392 | 身份容器 | local `deque` | 换类型即可 |
| core/debug_core.h:260 | 读图 | self | 换类型即可 |
| core/debug_core.cpp:465 | 读图 | `debug_core.h:260` | 换类型即可 |
| core/frames.cpp:83 | 读图 | `program_index.h:225` | 换类型即可 |
| core/frames.cpp:102 | 读图 | `program_index.h:225` | 换类型即可 |
| core/program_index.h:208 | 读图 | self | 换类型即可 |
| core/program_index.h:210 | 读图 | self | 换类型即可 |
| core/program_index.h:225 | 读图 | self | 换类型即可 |
| core/program_index.h:247 | 读图 | self | 换类型即可 |
| core/program_index.h:284 | 读图 | self | 换类型即可 |
| core/program_index.cpp:334 | 读图 | `program_index.h:208` | 换类型即可 |
| core/program_index.cpp:361 | 读图 | `program_index.h:210` | 换类型即可 |
| core/program_index.cpp:380 | 读图 | `program_index.h:284` | 换类型即可 |
| core/program_index.cpp:513 | 读图 | `program_index.h:247` | 换类型即可 |
| core/program_index.cpp:548 | 读图 | local candidate list | 换类型即可 |
| core/program_index.cpp:549 | 读图 | local map | 换类型即可 |
| core/program_index.cpp:569 | 读图 | local candidate list | 换类型即可 |
| core/program_index.cpp:755 | 读图 | `program_index.h:225` | 换类型即可 |

## Counts

- 回调签名 10 (one of them, `program_index.cpp:326`, may reach the kernel)
- 身份容器 25
- 读图 17
- 写图 0 (no `GraphDraft` site in this tree's `src/tools`)
- total 52

## Work estimate

One refactor, not three batches: headers (`debug_core.h`, `program_index.h`) first, then the `.cpp`s;
`GraphDraft` needs no work at all. That is one commit pair and **one build + one plan run**, provided
`traceGraphs` turns out to accept a `GraphView` overload on the kernel side; if it does not, that one
call site is a second, separate commit and a second build.