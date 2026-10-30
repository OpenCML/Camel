# Camel for AI — standard library signatures

Every name the standard library exports, with its signature. This is Appendix B of
[Camel for AI](camel-for-ai.md), and it is a document of its own because the library keeps gaining
names: the page an AI reads whole must not grow with the declarations, and this catalogue is looked
up rather than read.

`name: <with-parameters> (parameters) => result`: the ordinary parameters are the data the function
works on, the `with` parameters are what configures it, and a call chains them —
`s->trim->split<','>`, `xs->filter<p>->sort_by<key>`. `int` is the 64-bit integer and `float` the
64-bit float. A result `T!` is an error to be handled (`!!`, `->`, `match`), `T?` a value that may
be absent (`??`). A signature whose declaration carries type parameters is printed without them:
`is_numeric: () => bool` is declared `is_numeric::<T>()` and is written with its type argument when
the arguments do not fix it. `Tensor` is the `tensor` module's type name (the page, §12.2).

Source: `docs/stdlib-reference.md`, generated from the declarations in `stdlib/*.cml` and
`stdlib/signatures/*.cml` by `scripts/stdlib-reference.mjs --write`. Regenerate this region with
`node scripts/camel-for-ai.mjs --write`; regenerate the page it reads with
`node scripts/stdlib-reference.mjs --write`.

<!-- camel-for-ai region: stdlib-signatures (generated from docs/stdlib-reference.md; node scripts/camel-for-ai.mjs --write) -->
**`Built-in functions`** — no import — 65 name(s)

```text
std::mem::blocks: () => { allocs: int, frees: int, live: int, bytes: int }
int: (x: i32) => int
int: (x: int) => int
int: (x: f32) => int
int: (x: float) => int
int: (x: string) => int
int: (x: bf16) => int
int: (x: f16) => int
i64: (x: i32) => int
i64: (x: int) => int
i64: (x: f32) => int
i64: (x: float) => int
i64: (x: string) => int
i64: (x: bf16) => int
i64: (x: f16) => int
i32: (x: i32) => i32
i32: (x: int) => i32
i32: (x: f32) => i32
i32: (x: float) => i32
i32: (x: string) => i32
i32: (x: bf16) => i32
i32: (x: f16) => i32
float: (x: i32) => float
float: (x: int) => float
float: (x: f32) => float
float: (x: float) => float
float: (x: string) => float
float: (x: bf16) => float
float: (x: f16) => float
f64: (x: i32) => float
f64: (x: int) => float
f64: (x: f32) => float
f64: (x: float) => float
f64: (x: string) => float
f64: (x: bf16) => float
f64: (x: f16) => float
f32: (x: i32) => f32
f32: (x: int) => f32
f32: (x: f32) => f32
f32: (x: float) => f32
f32: (x: string) => f32
f32: (x: bf16) => f32
f32: (x: f16) => f32
bf16: (x: i32) => bf16
bf16: (x: int) => bf16
bf16: (x: f32) => bf16
bf16: (x: float) => bf16
bf16: (x: bf16) => bf16
bf16: (x: string) => bf16
f16: (x: i32) => f16
f16: (x: int) => f16
f16: (x: f32) => f16
f16: (x: float) => f16
f16: (x: f16) => f16
f16: (x: string) => f16
i32: (x: A) => WithElement<A, i32> when is_compact_array::<A>()
i64: (x: A) => WithElement<A, int> when is_compact_array::<A>()
int: (x: A) => WithElement<A, int> when is_compact_array::<A>()
f32: (x: A) => WithElement<A, f32> when is_compact_array::<A>()
f64: (x: A) => WithElement<A, float> when is_compact_array::<A>()
float: (x: A) => WithElement<A, float> when is_compact_array::<A>()
bf16: (x: A) => WithElement<A, bf16> when is_compact_array::<A>()
f16: (x: A) => WithElement<A, f16> when is_compact_array::<A>()
c64: (x: i32) => c64
c64: (x: int) => c64
c64: (x: f32) => c64
c64: (x: float) => c64
c64: (x: c64) => c64
c64: (x: c128) => c64
c128: (x: i32) => c128
c128: (x: int) => c128
c128: (x: f32) => c128
c128: (x: float) => c128
c128: (x: c64) => c128
c128: (x: c128) => c128
string: (x: T) => string
parse_int: (text: string) => int!
parse_float: (text: string) => float!
error: (kind: string, message: string) => Error
print: <fmt: string> (x: T, ..rest) => T
print: (x: T, ..rest) => T
print: () => void
println: <fmt: string> (x: T, ..rest) => T
println: (x: T, ..rest) => T
println: () => void
eprintln: <fmt: string> (x: T, ..rest) => T
eprintln: (x: T, ..rest) => T
eprintln: () => void
format: <..args> (fmt: string) => string
join: <sep: string> (xs: string[]) => string
len: (x: string) => int
len: (x: A) => int when is_array::<A>()
len: (x: Bytes) => int
range: (start: int, end: int, step: int) => int[]
last: (xs: T[]) => T
slice: <axis: int, start: int, end: int, step: int = 1> (x: T[]) => T[]
slice: <axis: int, start: int, end: int, step: int = 1> (x: string) => string
slice: <axis: int, start: int, end: int, step: int = 1> (x: Bytes) => Bytes
concat: (xs: T[], ys: T[]) => T[]
contains: <value: T> (xs: T[]) => bool
contains: <sub: string> (s: string) => bool
zip: (..xs: ..A) => (..ElementOf<A>)[] when ..is_array::<A>()
append: <value: T> (xs: T[]) => T[]
map: <f: (item: T) => U> (xs: T[]) => U[]
filter: <f: (item: T) => bool> (xs: T[]) => T[]
reduce: <f: (acc: U, item: T) => U, initial: U> (xs: T[]) => U
foreach: <f: (item: T) => void> (xs: T[]) => void
fail: <Kind: string> (message: string) => never
attempt: (f: F) => F!
len: (x: C) => int when is_map::<C>() || is_set::<C>()
get: <key: K> (m: Map<K, V>) => V?
get_or: <key: K, fallback: V> (m: Map<K, V>) => V
put: <key: K, value: V> (m: Map<K, V>) => Map<K, V>
remove: <key: K> (c: Map<K, V>) => Map<K, V>
remove: <key: K> (c: Set<K>) => Set<K>
has: <key: K> (c: Map<K, V>) => bool
keys: (m: Map<K, V>) => K[]
values: (m: Map<K, V>) => V[]
items: (m: Map<K, V>) => (K, V)[]
items: (m: Set<T>) => T[]
merge: (m: Map<K, V>, other: Map<K, V>) => Map<K, V>
contains: <value: T> (s: Set<T>) => bool
add: <value: T> (s: Set<T>) => Set<T>
union: (s: Set<T>, other: Set<T>) => Set<T>
intersect: (s: Set<T>, other: Set<T>) => Set<T>
difference: (s: Set<T>, other: Set<T>) => Set<T>
is_subset: (s: Set<T>, other: Set<T>) => bool
tee_note: (hint: int, cap: int, elements: int, peak: int, s: T..) => void
```

**`str`** — import { ... } from str — 16 name(s)

```text
codepoints: (s: string) => int[]
from_codepoints: (xs: int[]) => string!
to_upper: (s: string) => string
to_lower: (s: string) => string
index_of: <sub: string> (s: string) => int?
starts_with: <prefix: string> (s: string) => bool
ends_with: <suffix: string> (s: string) => bool
strip_prefix: <prefix: string> (s: string) => string?
strip_suffix: <suffix: string> (s: string) => string?
split: <sep: string> (s: string) => string[]
lines: (s: string) => string[]
replace: <old: string, new: string> (s: string) => string
repeat: <count: int> (s: string) => string
trim_start: (s: string) => string
trim_end: (s: string) => string
trim: (s: string) => string
```

**`bytes`** — import { ... } from bytes — 4 name(s)

```text
encode: (text: string) => Bytes
decode: (content: Bytes) => string!
from_array: (values: int[]) => Bytes
to_array: (content: Bytes) => int[]
```

**`ckpt`** — import { ... } from ckpt — 3 name(s)

```text
tree_leaves: (tree: T) => L
save_tree: <dir: string, dataset_manifest_sha256: string? = null> (tree: T) => void! uses fs when has_ckpt_form::<T>()
load_tree: <dataset_manifest_sha256: string? = null> (dir: string) => T! uses fs, stderr when has_ckpt_form::<T>()
```

**`tree`** — import { ... } from tree — 4 name(s)

```text
tree_paths: (t: T, here: string = '') => ?
tree_map: <f: F> (t: T, ..ts) => ?
tree_reduce: <f: (acc: R, leaf: A) => R> (t: T, init: R) => R
tree_unzip: (r: R, s: S) => ?
```

**`tensor`** — import { ... } from tensor — 61 name(s)

```text
zeros: (shape: int[]) => T[..] when is_numeric::<T>()
ones: (shape: int[]) => T[..] when is_numeric::<T>()
full: (shape: int[], value: number) => float[..]
eye: (n: int) => T[..] when is_numeric::<T>()
maximum: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
minimum: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
where: (cond: bool[..], lhs: float[..] | number, rhs: float[..] | number) => float[..]
abs: (t: float[..]) => float[..]
exp: (t: float[..]) => float[..]
log: (t: float[..]) => float[..]
sqrt: (t: float[..]) => float[..]
rsqrt: (t: float[..]) => float[..]
sigmoid: (t: float[..]) => float[..]
tanh: (t: float[..]) => float[..]
relu: (t: float[..]) => float[..]
gelu: (t: float[..]) => float[..]
erf: (t: float[..]) => float[..]
erfc: (t: float[..]) => float[..]
reshape: (t: float[..] | number[], shape: int[]) => float[..]
unsqueeze: (t: float[..], axis: int) => float[..]
transpose: (t: float[..]) => float[..]
permute: (t: float[..], perm: int[]) => float[..]
concat: (a: float[..] | number[], b: float[..] | number[], axis: int) => float[..]
take: (t: float[..], indices: int64[..] | number[]) => float[..]
one_hot: (labels: int64[..] | number[], classes: int) => int64[..]
shape: (t: T[..]) => int[] when is_numeric::<T>()
numel: (t: T[..]) => int when is_numeric::<T>()
dim: <axis: int> (t: T[..]) => int when is_numeric::<T>()
dim: <axis: i32> (t: T[..]) => int when is_numeric::<T>()
sum: (t: T[..]) => T
sum: (t: float[..], axis: int, keepdims?: bool) => float[..]
mean: (t: T[..]) => T
mean: (t: float[..], axis: int, keepdims?: bool) => float[..]
max: (t: float[..], axis: int, keepdims?: bool) => float[..]
min: (t: float[..], axis: int, keepdims?: bool) => float[..]
argmax: (t: float[..], axis: int, keepdims?: bool) => int64[..]
argmin: (t: float[..], axis: int, keepdims?: bool) => int64[..]
softmax: (t: f32[..], axis?: int) => f32[..]
log_softmax: (t: f32[..], axis?: int) => f32[..]
softmax_grad: (y: f32[..], dy: f32[..], axis?: int) => f32[..]
layer_norm: (x: f32[..], gamma: f32[..], beta: f32[..], eps?: number) => f32[..]
matmul: (lhs: float[..], rhs: float[..]) => float[..]
linear: (x: f32[..], weight: f32[..], bias?: f32[..]) => f32[..]
zeros_like: (t: float[..]) => float[..]
ones_like: (t: float[..]) => float[..]
load_npy: (path: string) => T[..]
show: (t: A[..]) => void
from_bytes: (bytes: Bytes, shape: int[]) => T[..] when is_plain::<T>()
to_bytes: (t: T[..]) => Bytes when is_plain::<T>()
save_npy: (t: T[..], path: string) => void when is_numeric::<T>()
num_threads: () => int
set_num_threads: (n: int) => void
set_num_threads: (n: i32) => void
gemm_backend: () => string
gemm_threads: () => int
gemm_library_threads: () => int
gemm_library: () => string
sum_axis: (xs: T[..], axis: int, keepdims: bool) => T[..] when is_numeric::<T>()
mean_axis: (xs: T[..], axis: int, keepdims: bool) => T[..] when is_numeric::<T>()
min_axis: (xs: T[..], axis: int, keepdims: bool) => T[..] when is_numeric::<T>()
max_axis: (xs: T[..], axis: int, keepdims: bool) => T[..] when is_numeric::<T>()
argmax_axis: (xs: T[..], axis: int, keepdims: bool) => int[] when is_numeric::<T>()
argmin_axis: (xs: T[..], axis: int, keepdims: bool) => int[] when is_numeric::<T>()
```

**`types`** — import { ... } from types — 16 name(s)

```text
is_same: () => bool
is_integer: () => bool
is_float: () => bool
is_complex: () => bool
is_numeric: () => bool
is_real: () => bool
is_ordered: () => bool
is_function: () => bool
is_array: () => bool
is_nullable: () => bool
is_fallible: () => bool
is_map: () => bool
is_set: () => bool
is_string: () => bool
is_bool: () => bool
is_plain: () => bool
```

**`array`** — import { ... } from array — 37 name(s)

```text
reverse: (xs: T[]) => T[]
take: <n: int> (xs: T[]) => T[]
drop: <n: int> (xs: T[]) => T[]
chunks: <n: int> (xs: T[]) => T[][]
enumerate: (xs: T[]) => (int, T)[]
flatten: (xss: T[][]) => T[]
flatten: (xs: T[..]) => T[] when !is_array::<T>()
unique: (xs: T[]) => T[]
find_index: <pred: (x: T) => bool> (xs: T[]) => int?
find: <pred: (x: T) => bool> (xs: T[]) => T?
index_of: <x: T> (xs: T[]) => int?
any: <pred: (x: T) => bool> (xs: T[]) => bool
all: <pred: (x: T) => bool> (xs: T[]) => bool
count: <pred: (x: T) => bool> (xs: T[]) => int
sum: (xs: T[..]) => T when is_numeric::<T>()
mean: (xs: T[..]) => T when is_float::<T>()
min: (xs: T[..]) => T when is_ordered::<T>()
max: (xs: T[..]) => T when is_ordered::<T>()
argmax: (xs: T[..]) => int when is_ordered::<T>()
argmin: (xs: T[..]) => int when is_ordered::<T>()
sum: <axis: int, keepdims: bool = false> (xs: T[..]) => T[..] when is_integer::<T>() || is_float::<T>()
mean: <axis: int, keepdims: bool = false> (xs: T[..]) => T[..] when is_float::<T>()
min: <axis: int, keepdims: bool = false> (xs: T[..]) => T[..] when is_integer::<T>() || is_float::<T>()
max: <axis: int, keepdims: bool = false> (xs: T[..]) => T[..] when is_integer::<T>() || is_float::<T>()
argmax: <axis: int, keepdims: bool = false> (xs: T[..]) => int[..] when is_integer::<T>() || is_float::<T>()
argmin: <axis: int, keepdims: bool = false> (xs: T[..]) => int[..] when is_integer::<T>() || is_float::<T>()
shape: (xs: T[..]) => int[]
numel: (xs: T[..]) => int
dim: <axis: int> (xs: T[..]) => int
to_bytes: (xs: T[..]) => Bytes when is_plain::<T>()
argsort: (keys: i32[]) => int[]
argsort: (keys: int[]) => int[]
argsort: (keys: f32[]) => int[]
argsort: (keys: float[]) => int[]
argsort: (keys: bf16[]) => int[]
argsort: (keys: f16[]) => int[]
argsort: (keys: bool[]) => int[]
argsort: (keys: string[]) => int[]
sort: (xs: T[]) => T[] when is_ordered::<T>()
merge_sorted: (xs: T[], ys: T[]) => T[] when is_ordered::<T>()
sort_by: <key: (x: T) => K> (xs: T[]) => T[] when is_ordered::<K>()
flat_map: <f: (x: T) => U[]> (xs: T[]) => U[]
zip_with: <f: (a: T, b: U) => V> (xs: T[], ys: U[]) => V[]
vmap: (f: (row: T) => U) => (xs: RowsOf<T>) => U[]
```

**`stream`** — import { ... } from stream — 12 name(s)

```text
unfold: (state: S, step: (state: S) => (T, S)?) => T..
empty: () => T..
from_array: (xs: T[]) => T..
iterate: <f: (x: T) => T> (x: T) => T..
map: <f: (x: T) => U> (s: T..) => U..
filter: <pred: (x: T) => bool> (s: T..) => T..
take: <n: int> (s: T..) => T..
scan: <f: (acc: A, x: T) => A, init: A> (s: T..) => A..
chunks: <k: int> (s: T..) => T[]..
zip: (a: T.., b: U..) => (T, U)..
collect: (s: T..) => T[]
tee: <n: int = 8> (s: T..) => (T.., T..)
```

**`math`** — import { ... } from math — 50 name(s)

```text
pi: float
e: float
inf: float
nan: float
sqrt: (x: T) => T when is_float::<T>()
exp: (x: T) => T when is_float::<T>()
exp: (z: T) => T when is_complex::<T>()
expm1: (x: T) => T when is_float::<T>()
log: (x: T) => T when is_float::<T>()
log2: (x: T) => T when is_float::<T>()
log10: (x: T) => T when is_float::<T>()
log1p: (x: T) => T when is_float::<T>()
pow: (x: T, y: T) => T when is_float::<T>()
hypot: (x: T, y: T) => T when is_float::<T>()
sin: (x: T) => T when is_float::<T>()
cos: (x: T) => T when is_float::<T>()
tan: (x: T) => T when is_float::<T>()
asin: (x: T) => T when is_float::<T>()
acos: (x: T) => T when is_float::<T>()
atan: (x: T) => T when is_float::<T>()
atan2: (y: T, x: T) => T when is_float::<T>()
sinh: (x: T) => T when is_float::<T>()
cosh: (x: T) => T when is_float::<T>()
tanh: (x: T) => T when is_float::<T>()
erf: (x: T) => T when is_float::<T>()
erfc: (x: T) => T when is_float::<T>()
floor: (x: T) => T when is_float::<T>()
ceil: (x: T) => T when is_float::<T>()
round: (x: T) => T when is_float::<T>()
trunc: (x: T) => T when is_float::<T>()
min: (a: T, b: T) => T when is_ordered::<T>()
max: (a: T, b: T) => T when is_ordered::<T>()
clamp: (x: T, lo: T, hi: T) => T when is_real::<T>()
abs: (x: T) => T when is_real::<T>()
abs: (z: T) => f32 when is_same::<T, c64>()
abs: (z: T) => float when is_same::<T, c128>()
sign: (x: T) => T when is_real::<T>()
is_nan: (x: T) => bool when is_float::<T>()
is_inf: (x: T) => bool when is_float::<T>()
is_finite: (x: T) => bool when is_float::<T>()
re: (z: c64) => f32
re: (z: c128) => float
im: (z: c64) => f32
im: (z: c128) => float
conj: (z: T) => T when is_same::<T, c64>()
conj: (z: T) => T when is_same::<T, c128>()
gcd: (a: T, b: T) => T when is_integer::<T>()
lcm: (a: T, b: T) => T when is_integer::<T>()
bit_and: (a: T, b: T) => T when is_same::<T, i32>()
bit_and: (a: T, b: T) => T when is_same::<T, int>()
bit_or: (a: T, b: T) => T when is_same::<T, i32>()
bit_or: (a: T, b: T) => T when is_same::<T, int>()
bit_xor: (a: T, b: T) => T when is_same::<T, i32>()
bit_xor: (a: T, b: T) => T when is_same::<T, int>()
shl: <count: int> (x: T) => T when is_same::<T, i32>()
shl: <count: int> (x: T) => T when is_same::<T, int>()
shr: <count: int> (x: T) => T when is_same::<T, i32>()
shr: <count: int> (x: T) => T when is_same::<T, int>()
```

**`statistics`** — import { ... } from statistics — 4 name(s)

```text
variance: (xs: T[]) => T when is_float::<T>()
stdev: (xs: T[]) => T when is_float::<T>()
median: (xs: T[]) => T when is_float::<T>()
quantile: <q: float> (xs: T[]) => T when is_float::<T>()
```

**`random`** — import { ... } from random — 16 name(s)

```text
key: (seed: int) => Key
fold: (k: Key, i: int) => Key
split: (k: Key) => (Key, Key)
uniform: (k: Key) => T when is_float::<T>()
uniform: <shape: int[]> (k: Key) => T[..] when is_float::<T>()
normal: (k: Key) => T when is_float::<T>()
normal: <shape: int[]> (k: Key) => T[..] when is_float::<T>()
randint: <low: int, high: int> (k: Key) => int
permutation: (k: Key, n: int) => int[]
shuffle: <k: Key> (xs: T[]) => T[]
choice: <k: Key> (xs: T[]) => T
sample: <k: Key, n: int> (xs: T[]) => T[]
seed: (value: int) => void uses rng
next_key: () => Key uses rng
rand: () => float
randn: () => float
```

**`time`** — import { ... } from time — 3 name(s)

```text
now: () => float
strftime: <format: string> (timestamp: float) => string
strptime: <format: string> (text: string) => float!
```

**`fs`** — import { ... } from fs — 16 name(s)

```text
read_text: (path: string) => string!
read_bytes: (path: string) => Bytes!
read_chunk: <offset: int, size: int> (path: string) => Bytes!
write_text: <path: string, atomic: bool = false> (content: string) => void!
write_bytes: <path: string, atomic: bool = false> (content: Bytes) => void!
append_text: <path: string> (content: string) => void!
append_bytes: <path: string> (content: Bytes) => void!
exists: (path: string) => bool
is_file: (path: string) => bool
is_dir: (path: string) => bool
file_size: (path: string) => int!
list_dir: (path: string) => string[]!
mkdir: (path: string) => void!
mkdirs: (path: string) => void!
remove: (path: string) => void!
remove_dir: (path: string) => void!
```

**`io`** — import { ... } from io — 6 name(s)

```text
set_terminal_raw_mode: (enabled: bool) => void!
input: (prompt: string) => string
has_input: () => bool
get_chars: (max: int) => string
get_char: () => string?
clear_input_buffer: () => void
```

**`process`** — import { ... } from process — 4 name(s)

```text
sleep: (seconds: float) => void
exit: (code: int) => never uses process
user_name: () => string!
cpu_count: () => int
```

**`env`** — import { ... } from env — 2 name(s)

```text
get_env: (name: string) => string?
get_cwd: () => string!
```

**`json`** — import { ... } from json — 13 name(s)

```text
decode: (text: string) => Json!
kind: (j: Json) => string
is_null: (j: Json) => bool
get: <key: string> (j: Json) => Json?
get: <key: string> (j: Json?) => Json?
get: <key: string> (j: Json!) => Json!?
get: <key: string> (j: Json!?) => Json!?
get: <key: int> (j: Json) => Json?
get: <key: int> (j: Json?) => Json?
get: <key: int> (j: Json!) => Json!?
get: <key: int> (j: Json!?) => Json!?
as_bool: (j: Json) => bool?
as_bool: (j: Json?) => bool?
as_bool: (j: Json!) => bool!?
as_bool: (j: Json!?) => bool!?
as_int: (j: Json) => int?
as_int: (j: Json?) => int?
as_int: (j: Json!) => int!?
as_int: (j: Json!?) => int!?
as_float: (j: Json) => float?
as_float: (j: Json?) => float?
as_float: (j: Json!) => float!?
as_float: (j: Json!?) => float!?
as_string: (j: Json) => string?
as_string: (j: Json?) => string?
as_string: (j: Json!) => string!?
as_string: (j: Json!?) => string!?
as_array: (j: Json) => Json[]?
as_array: (j: Json?) => Json[]?
as_array: (j: Json!) => Json[]!?
as_array: (j: Json!?) => Json[]!?
as_object: (j: Json) => Map<string, Json>?
as_object: (j: Json?) => Map<string, Json>?
as_object: (j: Json!) => Map<string, Json>!?
as_object: (j: Json!?) => Map<string, Json>!?
encode: <indent: int? = null> (value: T) => string when has_json_form::<T>()
decode_as: (source: string) => T! when has_json_form::<T>()
decode_as: (source: Json) => T! when has_json_form::<T>()
```

**`hash`** — import { ... } from hash — 2 name(s)

```text
sha256: (data: Bytes) => string
sha256: (text: string) => string
```

**`re`** — import { ... } from re — 7 name(s)

```text
validate: (pattern: string) => string!
find: <pattern: string, start: int = 0> (text: string) => Match?
has_match: <pattern: string> (text: string) => bool
find_all: <pattern: string> (text: string) => Match[]
replace_with: <pattern: string, by: (m: Match) => string> (text: string) => string
replace: <pattern: string, new: string> (text: string) => string
split: <pattern: string> (text: string) => string[]
```

**`para`** — import { ... } from para — 2 name(s)

```text
unordered_foreach: <f: (item: T) => void> (xs: T[]) => void
unordered_reduce: <f: (acc: T, item: T) => T, initial: T> (xs: T[]) => T
```

**`test_support`** — import { ... } from test_support — 5 name(s)

```text
rendezvous: (parties: int, timeout: float) => int uses rendezvous_point
thread_count: () => int uses process
with_probe: <base: int, step: int = 10, mode: int = 0> (x: T) => int
with_probe: <base: int, step: int = 10, mode: int = 0> (xs: int[]) => int
arity_probe: <tag: int, extra: int[] = []> (f: F, example: E) => int when is_function::<F>()
```

**`this`** — import { ... } from this — 1 name(s)

```text
zen: () => string
```

**`graph`** — import { ... } from graph — 42 name(s)

```text
graph_of: (f: F) => Graph when is_function::<F>()
graphs_of: (f: F) => Graph[] when is_function::<F>()
name: (g: Graph) => string
nodes: (g: Graph) => Node[]
inputs: (n: Node) => Node[]
with_inputs: (n: Node) => Node[]
sync_inputs: (n: Node) => Node[]
order_inputs: (n: Node) => Node[]
users: (n: Node) => Node[]
value_users: (n: Node) => Node[]
kind: (n: Node) => NodeKind
op: (n: Node) => Op?
op_name: (o: Op) => string
is_pure: (n: Node) => bool
is_anchor: (n: Node) => bool
is_written_slot: (n: Node) => bool
is_constant: (n: Node) => bool
type_of: (n: Node) => TypeRef?
type_name: (t: TypeRef) => string
callee: (n: Node) => Graph?
tags: (n: Node) => string
declaration_tags: (g: Graph) => Tag[]
tag_name: (t: Tag) => string
tag_graphs: (t: Tag) => Graph[]
rule_of: (g: Graph, name: string) => Graph?
origin: (n: Node) => int
constant: (n: Node) => T? when is_ordered::<T>()
region_of: (n: Node) => Region
region_kind: (r: Region) => RegionKind
region_parent: (r: Region) => Region?
show: (n: Node) => string
id: (h: Graph) => int
id: (h: Node) => int
id: (h: Op) => int
effects: (g: Graph) => Effects
shape_text: (n: Node) => string?
permissions: (n: Node) => int
open: (f: F) => Txn<F> when is_function::<F>()
replace: (t: Txn<F>, old: Node, new: Node) => void uses t
erase: (t: Txn<F>, n: Node) => void uses t
eval: (t: Txn<F>, n: Node) => Node? uses t
add_constant: (t: Txn<F>, g: Graph, value: T) => Node uses t when is_ordered::<T>()
commit: (t: Txn<F>) => F! uses t
abort: (t: Txn<F>) => void uses t
```

**`rewrite`** — import { ... } from rewrite — 15 name(s)

```text
rewrite: <rules: Rule[], strict: bool = false> (f: F) => F! when is_function::<F>()
saturate: <rules: Rule[], rounds: int = 6, strict: bool = false> (f: F) => F! when is_function::<F>()
extract: <cost: (o: Op, inputs: float[]) => float> (f: F) => F! when is_function::<F>()
run: <rules: Rule[], rounds: int = 16, strict: bool = false, explain: bool = false> (f: F) => Run<F>! when is_function::<F>()
check: <samples: int = 100, seed: int = 0> (rules: Rule[]) => CheckReport
check: <over: F, samples: int = 100, seed: int = 0> (rules: Rule[]) => CheckReport when is_function::<F>()
check_text: (r: CheckReport) => string
coverage_table: string
explain: <a: Node, b: Node> (r: Run<F>) => Proof!
explain_all: (r: Run<F>) => Proof!
why_not: <rule: Rule, v: Node> (r: Run<F>) => string!
rule_name: (r: Rule) => string
rule_two_way: (r: Rule) => bool
rule_needs: (r: Rule) => int
rule_consts: (r: Rule) => int
```

**`tiles`** — import { ... } from tiles — 9 name(s)

```text
upload: (value: f32[..]) => Device<f32[..]> uses driver
download: (value: Device<f32[..]>) => f32[..] uses driver
run_preferred_resident: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => Device<f32> uses driver
source: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string uses driver
inspect: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string uses driver
benchmark: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string uses driver
regions: <fn: (a: f32, b: f32) => f32> () => string uses driver
available: () => bool uses driver
device: () => string! uses driver
```

**`pyplot`** — import { ... } from pyplot — 1 name(s)

```text
plot: (data: i32[]) => void
plot: (data: int[]) => void
plot: (data: f32[]) => void
plot: (data: float[]) => void
plot: (data: i32[], filename: string) => void
plot: (data: int[], filename: string) => void
plot: (data: f32[], filename: string) => void
plot: (data: float[], filename: string) => void
```

**`python`** — import { ... } from python — 9 name(s)

```text
exec: (code: string) => void!
eval: (expression: string) => PyObject!
run_file: (path: string) => void!
import_module: (name: string) => PyObject!
attr: <name: string> (obj: PyObject) => PyObject!
call: <fn: PyObject, kwargs: Map<string, PyObject>? = null> (args: PyObject[]) => PyObject!
to_string: (obj: PyObject) => string!
to_py: (x: T) => PyObject! when has_python_form::<T>()
from_py: (obj: PyObject) => T! when has_python_form::<T>()
```

**`nn`** — import { ... } from nn — 19 name(s)

```text
conv2d: (input: f32[..], kernel: f32[..], bias: f32[..], stride?: int, padding?: int) => f32[..]
conv2d_input_grad: (input: f32[..], kernel: f32[..], dy: f32[..], stride?: int, padding?: int) => f32[..]
conv2d_kernel_grad: (input: f32[..], kernel: f32[..], dy: f32[..], stride?: int, padding?: int) => f32[..]
conv2d_bias_grad: (bias: f32[..], dy: f32[..]) => f32[..]
max_pool2d: (input: f32[..], kernel: int, stride?: int, padding?: int) => f32[..]
avg_pool2d: (input: f32[..], kernel: int, stride?: int, padding?: int) => f32[..]
max_pool2d_grad: (input: f32[..], dy: f32[..], kernel: int, stride?: int, padding?: int) => f32[..]
avg_pool2d_grad: (input: f32[..], dy: f32[..], kernel: int, stride?: int, padding?: int) => f32[..]
batch_norm: (input: f32[..], mean: f32[..], var: f32[..], gamma: f32[..], beta: f32[..], eps?: number) => f32[..]
batch_norm_train: (input: f32[..], gamma: f32[..], beta: f32[..], running_mean: f32[..], running_var: f32[..], momentum?: number, eps?: number) => (f32[..], f32[..], f32[..])
batch_norm_train_grad: (input: f32[..], gamma: f32[..], dy: f32[..], eps?: number) => (f32[..], f32[..], f32[..])
embedding: (table: f32[..], indices: f32[..]) => f32[..]
embedding_table_grad: (table: f32[..], indices: f32[..], dy: f32[..]) => f32[..]
softmax_cross_entropy: (logits: f32[..], target: f32[..]) => float
softmax_cross_entropy: (logits: f32[..], target: f32[..] | number[]) => float
softmax_cross_entropy_grad: (logits: f32[..], target: f32[..], dy: number) => f32[..]
softmax_cross_entropy_grad: (logits: f32[..], target: f32[..] | number[], dy: number) => f32[..]
sgd: (params: P, grads: P, lr: float) => P
adam_state: (params: P) => OptimizerState
adam: (params: P, grads: P, state: OptimizerState, lr: float, beta1?: number, beta2?: number, eps?: number) => (P, OptimizerState)
save_params: (params: P, dir: string) => void
```

**`optim`** — import { ... } from optim — 10 name(s)

```text
constant: (rate: float) => (step: int) => float
linear_decay: (peak: float, total: int, floor: float) => (step: int) => float
cosine_decay: (peak: float, total: int, floor: float) => (step: int) => float
sgd: (schedule: (step: int) => float) => Sgd
adam: (schedule: (step: int) => float, beta1: float = 0.9, beta2: float = 0.999, eps: float = 0.00000001, decay: float = 0.0) => Adam
adamw: (schedule: (step: int) => float, beta1: float = 0.9, beta2: float = 0.999, eps: float = 0.00000001, decay: float = 0.01) => Adam
init: (opt: Sgd, params: P) => SgdState
init: (opt: Adam, params: P) => AdamState<P>
update: (opt: Sgd, params: P, grads: G, state: SgdState) => (P, SgdState)
update: (opt: Adam, params: P, grads: G, state: AdamState<P>) => (P, AdamState<P>)
```

**`autodiff`** — import { ... } from autodiff — 15 name(s)

```text
grad: (f: F) => Gradient<F> when is_function::<F>()
grad: (f: F, has_aux: bool) => GradientAndAux<F> when is_function::<F>()
with_grad: (f: F) => ValueAndGradient<F> when is_function::<F>()
with_grad: (f: F, has_aux: bool) => ValueAndGradientAndAux<F> when is_function::<F>()
stop_gradient: (x: T) => T pure
add_tangent: (x: T, w: T, dx: T, dw: T) => T when is_float::<T>()
sub_tangent: (x: T, w: T, dx: T, dw: T) => T when is_float::<T>()
mul_tangent: (x: T, w: T, dx: T, dw: T) => T when is_float::<T>()
div_tangent: (x: T, w: T, dx: T, dw: T) => T when is_float::<T>()
neg_tangent: (x: T, dx: T) => T when is_float::<T>()
add_cotangent: (x: T, c: T, ct: T) => T when is_float::<T>()
sub_cotangent: (x: T, c: T, ct: T) => T when is_float::<T>()
mul_cotangent: (x: T, c: T, ct: T) => T when is_float::<T>()
div_cotangent: (x: T, c: T, ct: T) => T when is_float::<T>()
neg_cotangent: (x: T, ct: T) => T when is_float::<T>()
```

**`rulesets.jvp`** — import { ... } from rulesets.jvp — 7 name(s)

```text
lift: (x: T) => Lift<T>
mul_jvp: (x: f32, y: f32, d: (f32, f32)) => (f32, Tangent<f32>)
mul_jvp: (x: float, y: float, d: (float, float)) => (float, Tangent<float>)
sub_jvp: (x: f32, y: f32, d: (f32, f32)) => (f32, Tangent<f32>)
sub_jvp: (x: float, y: float, d: (float, float)) => (float, Tangent<float>)
div_jvp: (x: f32, y: f32, d: (f32, f32)) => (f32, Tangent<f32>)
div_jvp: (x: float, y: float, d: (float, float)) => (float, Tangent<float>)
```

**`sim`** — import { ... } from sim — 2 name(s)

```text
pi_hits: (samples: int, seed: int) => int
asian_call_price: (paths: int, steps: int, seed: int) => float
```

**`onnx`** — import { ... } from onnx — 2 name(s)

```text
supported_operators: () => string[]
export_model: <out: string, dynamic_axes: int[] = [], target: string = 'onnxruntime-cpu'> (fn: F, example: E) => void! when is_function::<F>()
```
<!-- camel-for-ai end: stdlib-signatures -->