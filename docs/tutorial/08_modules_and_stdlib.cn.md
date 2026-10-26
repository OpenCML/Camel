# 第 8 章 · 模块、标准库与一个小工具

[English](08_modules_and_stdlib.en.md) | 中文 · [返回目录](tutorial.cn.md)

到第 7 章为止，所有代码都写在一个文件里。真实的程序要用别人写好的东西：读一个文件、切开一段文本、把结果
写成 JSON。这一章讲怎么把代码分到多个文件、标准库里有什么，然后**把第一部分那个词频统计小工具写完**。

## 一个文件就是一个模块

**Camel 里没有 `module` 声明**：一个 `.cml` 文件就是一个模块，模块名就是文件名——`geometry.cml` 是模块
`geometry`。要让外面看得见，就写 `export`：

[`examples/geometry.cml`](examples/geometry.cml)：

```cml
// A library module used by 08_modules.cml: a file is a module, and its name is the file's name.
// Run: camel docs/tutorial/examples/08_modules.cml

import { sqrt } from math

export type Vec = { x: float, y: float }

export func hypot(v: Vec): float {
    return sqrt(v.x * v.x + v.y * v.y)
}

export func area(w: float, h: float): float {
    return w * h
}
```

用它的那一侧有两种写法，[`examples/08_modules.cml`](examples/08_modules.cml) 里都出现了：

```cml
// Importing from a standard module and from a file next to this one.
// Run: camel docs/tutorial/examples/08_modules.cml

import { sqrt } from math
import { hypot, Vec } from geometry
import geometry

func main(): int sync {
    println(sqrt(2.0))
    let v: Vec = { x: 3.0, y: 4.0 }
    println(hypot(v))
    println(geometry::area(2.0, 5.0))
    return 0
}
```

```bash
camel docs/tutorial/examples/08_modules.cml
```

```text
1.4142135623730951
5.0
10.0
```

- **`import { sqrt } from math`** 只把点名的名字拿进来，之后直接写 `sqrt(...)`。
- **`import geometry`** 把整个模块拿进来，用 `::` 写清楚它来自哪里：`geometry::area(...)`。名字撞车时这种写法
  更安全，读代码的人也不用回头找 `area` 是哪里来的。
- 模块在**编译期**解析：文件在同一个目录（或在 `CAMEL_PACKAGES` 给出的根下）。`export` 决定外面能见到
  哪些名字，没写 `export` 的名字是模块自己的事。

## 标准库就是一套模块

`math`、`str`、`fs`、`json`、`array`、`map`、`tensor` 等都是随 Camel 一起发布的模块，写法与自己写的模块
完全一样。这一章用到三个：

- [`fs`](../stdlib-reference.md#fs)：`path->read_text` 读一个文本文件（失败是错误值），`text->write_text<path>` 写一个。
- [`str`](../stdlib-reference.md#str)：`split<' '>` 按分隔符切开，`to_lower`、`trim` 整理单词。
- [`json`](../stdlib-reference.md#json)：`encode(value)` 把值写成一行 JSON，`decode(text)` 读一段 JSON。

完整的名单和每个函数的签名在[标准库参考](../stdlib-reference.md)里。

## 把词频统计写完

第 6 章的 `tally`（一个映射上的折叠）加上读文件与切开文本，就是第一部分的小工具：
[`examples/08_wordcount.cml`](examples/08_wordcount.cml)，文件的路径由 `--arg` 给（它就是 `main` 的参数）：

```cml
// The wordcount tool of part one: read a text, count the words, print the counts as JSON.
// Run: camel docs/tutorial/examples/08_wordcount.cml --arg path=docs/tutorial/examples/08_words.txt

import { read_text } from fs
import { split, to_lower, trim } from str
import { encode } from json

func words_of(text: string): string[] {
    return text->split<' '>->map<(w: string): string => to_lower(trim(w))>->filter<(w: string): bool => w != ''>
}

func tally(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(path: string): int sync {
    let words = words_of(path->read_text !! '')
    let counts = tally(words)
    println(len(words))
    println(encode(counts))
    return 0
}
```

```bash
camel docs/tutorial/examples/08_wordcount.cml --arg path=docs/tutorial/examples/08_words.txt
```

```text
9
{"brown":1,"dog":1,"fox":2,"lazy":1,"quick":1,"the":3}
```

三行就是全部：读进来、算出来、写出去。**能跑起来的最小工具比想象中短**——这正是第一部分想说明的事：
类型、函数、分支、数组、映射、失败处理各就各位之后，剩下的只是把它们接起来。

路径不写死在代码里，`main` 的参数就是它。**`main` 可以带参数**：`camel xxx.cml --arg path=...` 按声明
的类型读这个值（`string` 参数就是那段文本本身），`main` 返回的值就是程序的结果，`-o` 把它写进文件。这几条
在[命令行](../cli.md)里都有。

## 在图上

- `import` 是**编译期**的事：图上没有"模块边界"这种东西。用到的函数被解析成 `CALL` 节点，名字来自哪个文件
  不影响图的形状。
- `export` 只是**可见性**：它决定你在另一个文件里能不能写出那个名字，不产生任何运行时的东西。
- 所以"分文件"与"分函数"一样，是给人读的安排；图还是那个图。第 19 章会看到这也意味着一个模块可以被整个
  当成子图用。

## 练习

把工具包成 `tally(path)`：给它一个路径，它给出词频；再打印出不同词的个数和最常出现的词（`fox`）的次数。

参考答案 [`examples/08_exercise.cml`](examples/08_exercise.cml)：

```cml
// The answer to the exercise of chapter 8: the same tool, with the reading in one function.
// Run: camel docs/tutorial/examples/08_exercise.cml --arg path=docs/tutorial/examples/08_words.txt

import { read_text } from fs
import { split, to_lower, trim } from str
import { encode } from json

func words_of(text: string): string[] {
    return text->split<' '>->map<(w: string): string => to_lower(trim(w))>->filter<(w: string): bool => w != ''>
}

func tally(path: string): Map<string, int> {
    let words = words_of(path->read_text !! '')
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(path: string): int sync {
    let counts = tally(path)
    println(len(keys(counts)), counts['fox'] ?? 0, encode(counts))
    return 0
}
```

```bash
camel docs/tutorial/examples/08_exercise.cml --arg path=docs/tutorial/examples/08_words.txt
```

```text
6 2 {"brown":1,"dog":1,"fox":2,"lazy":1,"quick":1,"the":3}
```

## 参考

- [语言参考 5 模块与标准库](../language/05_modules.md)
- [标准库参考](../stdlib-reference.md)（[`fs`](../stdlib-reference.md#fs)、[`str`](../stdlib-reference.md#str)、[`json`](../stdlib-reference.md#json)）
- [命令行](../cli.md)（`--arg`、`-o`、标准输出）