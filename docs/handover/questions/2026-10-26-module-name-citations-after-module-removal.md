# 删掉 `module <m>` 之后，以模块名为符号的引用会失效

（一条从整合里掉出来的规律，附我们做的唯一一处修复，供你否决。）

## 现象

`module <m>` 这一行机械删掉后，`docs/**` 里以**模块名**为符号的引用（`…/foo.cml#para` 这种）就**无处可解析**了：解析器原本靠那一行把文件与模块名对上。

逐字（`--citations` 报的）：

```
no such file; 2 files are named 'para.cml' and none names 'para'
```

## 全仓计数

以模块名作符号的引用：**1 处**，即 `docs/design/op-semantics/README.cn.md:601`，它写的是 `stdlib/signatures/para.cml#para`。

## 我们做的修复（可否决）

这句话本身就是 `unordered_reduce` 的文档注释，所以改成指向 `stdlib/para.cml#unordered_reduce`；改后 `--citations` **418/418 绿**。

`docs/design/**` 全仓**只有这一处**改动；若你希望这类引用一律改成指文件里的具体声明（而不是模块名），说一声我们照办。