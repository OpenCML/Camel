# 88 主机（`ssh 88zjwei`）

## 是什么

| 项 | 值 |
|---|---|
| 别名 | `88zjwei`（= tangshan88，10.126.62.88，用户 `zjwei`） |
| 系统 | Ubuntu 22.04，32 核 EPYC 7343，503 GB 内存 |
| 磁盘 | 1.8 T，常年余 ~530 GB |
| GPU | 8× RTX 3090；驱动 CUDA 13，**没有 nvcc**（Tiles 的 GPU 编译跑不了） |
| sudo | **无免密**，所以不能 `apt install` |
| 网络 | 校园网认证门户：**直连不了 GitHub 与 pypi**。需要时开反向 SOCKS：`ssh -N -R 127.0.0.1:1080 88zjwei`（在主机上 `127.0.0.1:1080` 变成通向外面的出口） |

## 工具链

```
~/camel-eval/conda/envs/clang/bin/x86_64-conda-linux-gnu-clang++   # clang 23.1.2
~/camel-eval/venv-build/bin/cmake                                   # 4.4.3
~/camel-eval/venv-build/bin/ninja                                   # 1.13.2
~/camel-eval/venv-build/bin/conan                                   # 2.33.0
~/camel-eval/venv-bench/bin/python                                  # 3.10 + onnx 1.23 + onnxruntime 1.23
node                                                                 # 20.20.2
docker
```

`camel-remote.ps1` 每次运行都导出：`PATH`（上面三个 bin）、`CC=clang`、`CXX=clang++`、
`LD_LIBRARY_PATH=$C/lib`、`CMAKE_BUILD_PARALLEL_LEVEL`、`CAMEL_HOME=$R/out/latest`、
`CAMEL_TEST_TMP`。

## 目录

```
~/camel-work/Camel.git        # 裸库，git push 的中转远端
~/camel-work/<工作树名>/       # 每个任务一个常驻检出，带 build/ 与 out/
~/camel-work/bin/             # 工具 shim
/tmp/camel-remote-<名字>-<时刻>/  # 每次运行的暂存目录，跑完自己删
```

- 检出目录名 = 本机工作树目录名，一一对应；目录名只是标签，**分支才是身份**
  （同一个目录换分支时脚本会 `checkout -f -B`，不会串）。
- `build/` 每个 130–220 MB；构建是增量的，冷构建 120–220 s，热构建几十秒。

## 空间管理（脚本自动做，也可手动）

- 每次运行开头：删掉「本机已无对应工作树」的检出目录；清 `/tmp/camel-remote-*` 里一天前的；
  打印 `host: N GB free, M MB of checkouts`；剩余 < 5 GB 直接拒绝构建。
- `camel-remote.ps1 -Path <wt> -Clean`：删掉该工作树在主机上的检出（分支与提交在裸库里，不会丢）。
- **不要清 `~/.cache/pip`（25 GB）**：主机上不了 pypi，那是离线安装的唯一来源。`pip cache purge`
  只安全地清掉 4 GB 的 wheel 缓存，可做。
- `~/.cache/huggingface`（15 GB）是模型，别动。

## 主机特有的坑

| 症状 | 原因 | 解法 |
|---|---|---|
| `fatal error: 'x86_64-linux-gnu/python3.10/pyconfig.h' file not found` | conda clang 的 sysroot 没有 multiarch 目录 | `ln -s /usr/include/x86_64-linux-gnu $C/x86_64-conda-linux-gnu/sysroot/usr/include/` |
| `bash: line 27: exit: 0: numeric argument required` | 脚本带 CRLF | 脚本以文件发送，写入前 `-replace "\r\n","\n"`，用 `UTF8Encoding($false)` |
| `gpu_preference.no_device.*` 10.0x s 失败 | 主机负载高时撞 10 秒 `timeout_ms` | 用 `-Jobs 8`；单独重跑会过，不是回归 |
| 构建跑到一半进程消失 | 多人抢核 / 内存 | 看 `loadavg`，必要时降 `-Jobs` |
| 某个目录 `git apply` 失败 | 上次运行留下的脏树 | 脚本已 `checkout -f` + `clean -fd`；仍失败就 `-Clean` 重来 |

## 手工上主机时

```powershell
& "C:\Windows\System32\OpenSSH\ssh.exe" 88zjwei '命令'
& "C:\Windows\System32\OpenSSH\scp.exe" -q 本地文件 "88zjwei:/tmp/"
```

要注意的是：**PowerShell 的 `>` 会写 UTF-16**，生成文件用 `[System.IO.File]::WriteAllText` +
`UTF8Encoding($false)`；`git diff --output=` 而不是 `git diff > file`。