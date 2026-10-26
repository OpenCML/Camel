# Camel for AI — standard library signatures

Every name the standard library exports, with its signature. This is Appendix B of
[Camel for AI](camel-for-ai.md), and it is a document of its own because the library keeps gaining
names: the page an AI reads whole must not grow with the declarations, and this catalogue is looked
up rather than read.

`name: <with-parameters> (parameters) => result`: the ordinary parameters are the data the function
works on, the `with` parameters are what configures it, and a call chains them —
`s->trim->split<','>`, `xs->filter<p>->sort_by<key>`. `int` is the 64-bit integer and `float` the
64-bit float. A result `T!` is an error to be handled (`!!`, `?->`, `match`), `T?` a value that may
be absent (`??`). A signature whose declaration carries type parameters is printed without them:
`is_numeric: () => bool` is declared `is_numeric::<T>()` and is written with its type argument when
the arguments do not fix it. `Tensor` is the `tensor` module's type name (the page, §12.2).

Source: `docs/stdlib-reference.md`, generated from the declarations in `stdlib/*.cml` and
`stdlib/signatures/*.cml` by `scripts/stdlib-reference.mjs --write`. Regenerate this region with
`node scripts/camel-for-ai.mjs --write`; regenerate the page it reads with
`node scripts/stdlib-reference.mjs --write`.

<!-- camel-for-ai region: stdlib-signatures (generated from docs/stdlib-reference.md; node scripts/camel-for-ai.mjs --write) -->
**`Built-in functions`** — no import — 60 name(s)

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
print: (x: T, ...rest: U...) => T
println: (x: T, ...rest: U...) => T
eprintln: (x: T, ...rest: U...) => T
input: (prompt: string) => string
sleep: (milliseconds: int) => void
exit: (code: int) => void
format: <...args: T...> (fmt: string) => string
join: <sep: string> (xs: string[]) => string
len: (x: string) => int
len: (x: T[..]) => int
range: (start: int, end: int, step?: int) => int[]
head: (xs: T[]) => T
last: (xs: T[]) => T
tail: (xs: T[]) => T[]
slice: <start: int, end: int> (xs: T[]) => T[]
slice: (t: T[], axis: int, start: int, end: int, step: int = 1, written: int = 3) => T[]
concat: (xs: T[], ys: T[]) => T[]
contains: <value: T> (xs: T[]) => bool
contains: <sub: string> (s: string) => bool
zip: (xs: T[], ys: U[]) => (T, U)[]
append: <value: T> (xs: T[]) => T[]
extend: <other: T[]> (xs: T[]) => T[]
map: <f: (item: T) => U> (xs: T[]) => U[]
filter: <f: (item: T) => bool> (xs: T[]) => T[]
reduce: <f: (acc: U, item: T) => U, initial: U> (xs: T[]) => U
foreach: <f: (item: T) => void> (xs: T[]) => void
apply: <f: (item: T) => T> (xs: T[]) => T[]
attempt: (f: (...args: A) => T, ...args: A) => T!
len: (x: Map<K, V>) => int
len: (x: Set<T>) => int
get: <key: K> (m: Map<K, V>) => V?
get_or: <key: K, fallback: V> (m: Map<K, V>) => V
put: <key: K, value: V> (m: Map<K, V>) => Map<K, V>
remove: <key: K> (c: Map<K, V>) => Map<K, V>
remove: (c: Set<K>) => Set<K>
has: <key: K> (c: Map<K, V>) => bool
has: (c: Set<K>) => bool
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

**`str`** — import { ... } from str — 14 name(s)

```text
split: <sep: string> (s: string) => string[]
trim: (s: string) => string
trim_start: (s: string) => string
trim_end: (s: string) => string
to_upper: (s: string) => string
to_lower: (s: string) => string
replace: <from_: string, to: string> (s: string) => string
find: <sub: string> (s: string) => int?
substr: <start: int, length: int> (s: string) => string
starts_with: <prefix: string> (s: string) => bool
ends_with: <suffix: string> (s: string) => bool
repeat: <count: int> (s: string) => string
codepoints: (s: string) => int[]
from_codepoints: (xs: int[]) => string!
```

**`bytes`** — import { ... } from bytes — 6 name(s)

```text
encode: (text: string) => Bytes
decode: (content: Bytes) => string!
from_array: (values: int[]) => Bytes
to_array: (content: Bytes) => int[]
size: (content: Bytes) => int
concat: (left: Bytes, right: Bytes) => Bytes
```

**`ckpt`** — import { ... } from ckpt — 3 name(s)

```text
tree_leaves: (tree: T) => L
save_tree: (dir: string, tree: T, dataset_manifest_sha256?: string) => void
load_tree: (dir: string, dataset_manifest_sha256?: string) => T!
```

**`tree`** — import { ... } from tree — 4 name(s)

```text
tree_paths: (t: T, here: string = '') => ?
tree_map: <f: F> (t: T, ..ts) => ?
tree_reduce: <f: (acc: R, leaf: A) => R> (t: T, init: R) => R
tree_unzip: (r: R, s: S) => ?
```

**`tensor`** — import { ... } from tensor — 69 name(s)

```text
new: (values: float[..] | number[]) => float[..]
empty: (shape: int[]) => f32[..]
zeros: (shape: int[]) => f32[..]
ones: (shape: int[]) => f32[..]
full: (shape: int[], value: number) => float[..]
eye: (n: int) => f32[..]
range: (start: int, stop?: int, step?: int) => int64[..]
random: (shape: int[], low: number, high: number) => f32[..]
randn: (shape: int[], mean?: number, std?: number) => f32[..]
add: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
subtract: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
multiply: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
divide: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
pow: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
maximum: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
minimum: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
mod: (lhs: float[..] | number, rhs: float[..] | number) => float[..]
where: (cond: bool[..], lhs: float[..] | number, rhs: float[..] | number) => float[..]
neg: (t: float[..]) => float[..]
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
reshape: (t: float[..] | number[], shape: int[]) => float[..]
flatten: (t: float[..], axis?: int) => float[..]
unsqueeze: (t: float[..], axis: int) => float[..]
transpose: (t: float[..]) => float[..]
permute: (t: float[..], perm: int[]) => float[..]
concat: (a: float[..] | number[], b: float[..] | number[], axis: int) => float[..]
take: (t: float[..], indices: int64[..] | number[]) => float[..]
one_hot: (labels: int64[..] | number[], classes: int) => int64[..]
shape: (t: T[..]) => int[] when is_numeric::<T>()
numel: (t: T[..]) => int when is_numeric::<T>()
dim: (t: T[..], axis: int) => int when is_numeric::<T>()
dim: (t: T[..], axis: i32) => int when is_numeric::<T>()
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
cast: (t: float[..] | number[], dtype: string) => int64[..]
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
seed: (seed: int) => void
seed: (seed: i32) => void
```

**`types`** — import { ... } from types — 11 name(s)

```text
is_same: () => bool
is_integer: () => bool
is_float: () => bool
is_complex: () => bool
is_numeric: () => bool
is_real: () => bool
is_ordered: () => bool
is_function: () => bool
is_string: () => bool
is_bool: () => bool
is_plain: () => bool
```

**`array`** — import { ... } from array — 23 name(s)

```text
argsort: (keys: i32[]) => int[]
argsort: (keys: int[]) => int[]
argsort: (keys: f32[]) => int[]
argsort: (keys: float[]) => int[]
argsort: (keys: bool[]) => int[]
argsort: (keys: string[]) => int[]
sort: (xs: i32[]) => i32[]
sort: (xs: int[]) => int[]
sort: (xs: f32[]) => f32[]
sort: (xs: float[]) => float[]
insert_sort: (xs: i32[]) => i32[]
insert_sort: (xs: int[]) => int[]
insert_sort: (xs: f32[]) => f32[]
insert_sort: (xs: float[]) => float[]
quick_sort: (xs: i32[]) => i32[]
quick_sort: (xs: int[]) => int[]
quick_sort: (xs: f32[]) => f32[]
quick_sort: (xs: float[]) => float[]
merge_sort: (xs: i32[]) => i32[]
merge_sort: (xs: int[]) => int[]
merge_sort: (xs: f32[]) => f32[]
merge_sort: (xs: float[]) => float[]
merge_sorted_arrays: (left: i32[], right: i32[]) => i32[]
merge_sorted_arrays: (left: int[], right: int[]) => int[]
merge_sorted_arrays: (left: f32[], right: f32[]) => f32[]
merge_sorted_arrays: (left: float[], right: float[]) => float[]
reverse: (xs: T[]) => T[]
take: <n: int> (xs: T[]) => T[]
drop: <n: int> (xs: T[]) => T[]
enumerate: (xs: T[]) => (int, T)[]
flatten: (xss: T[][]) => T[]
unique: (xs: T[]) => T[]
index_of: <x: T> (xs: T[]) => int?
find: <pred: (x: T) => bool> (xs: T[]) => T?
any: <pred: (x: T) => bool> (xs: T[]) => bool
all: <pred: (x: T) => bool> (xs: T[]) => bool
count: <pred: (x: T) => bool> (xs: T[]) => int
sum: (xs: T[]) => T when is_numeric::<T>()
min: (xs: T[]) => T? when is_ordered::<T>()
max: (xs: T[]) => T? when is_ordered::<T>()
flat_map: <f: (x: T) => U[]> (xs: T[]) => U[]
sort_by: <key: (x: T) => K> (xs: T[]) => T[]
zip_with: <f: (a: T, b: U) => V> (xs: T[], ys: U[]) => V[]
```

**`stream`** — import { ... } from stream — 2 name(s)

```text
unfold: (state: S, step: (state: S) => (T, S)?) => T..
tee: (s: T.., n: int = 8) => (T.., T..)
```

**`math`** — import { ... } from math — 48 name(s)

```text
pi: float
e: float
inf: float
nan: float
sqrt: (x: i32) => f32
sqrt: (x: int) => float
sqrt: (x: f32) => f32
sqrt: (x: float) => float
abs: (x: i32) => i32
abs: (x: int) => int
abs: (x: f32) => f32
abs: (x: float) => float
abs: (x: c64) => f32
abs: (x: c128) => float
abs: (x: bf16) => bf16
abs: (x: f16) => f16
re: (x: c64) => f32
re: (x: c128) => float
im: (x: c64) => f32
im: (x: c128) => float
conj: (x: c64) => c64
conj: (x: c128) => c128
exp: (x: f32) => f32
exp: (x: float) => float
exp: (x: c64) => c64
exp: (x: c128) => c128
expm1: (x: f32) => f32
expm1: (x: float) => float
log: (x: f32) => f32
log: (x: float) => float
log2: (x: f32) => f32
log2: (x: float) => float
log10: (x: f32) => f32
log10: (x: float) => float
log1p: (x: f32) => f32
log1p: (x: float) => float
pow: (x: f32, y: f32) => f32
pow: (x: float, y: float) => float
hypot: (x: f32, y: f32) => f32
hypot: (x: float, y: float) => float
sin: (x: f32) => f32
sin: (x: float) => float
cos: (x: f32) => f32
cos: (x: float) => float
tan: (x: f32) => f32
tan: (x: float) => float
asin: (x: f32) => f32
asin: (x: float) => float
acos: (x: f32) => f32
acos: (x: float) => float
atan: (x: f32) => f32
atan: (x: float) => float
atan2: (y: f32, x: f32) => f32
atan2: (y: float, x: float) => float
sinh: (x: f32) => f32
sinh: (x: float) => float
cosh: (x: f32) => f32
cosh: (x: float) => float
tanh: (x: f32) => f32
tanh: (x: float) => float
floor: (x: f32) => f32
floor: (x: float) => float
ceil: (x: f32) => f32
ceil: (x: float) => float
round: (x: f32) => f32
round: (x: float) => float
trunc: (x: f32) => f32
trunc: (x: float) => float
min: (a: i32, b: i32) => i32
min: (a: int, b: int) => int
min: (a: f32, b: f32) => f32
min: (a: float, b: float) => float
min: (a: string, b: string) => string
min: (a: bool, b: bool) => bool
min: (a: bf16, b: bf16) => bf16
min: (a: f16, b: f16) => f16
max: (a: i32, b: i32) => i32
max: (a: int, b: int) => int
max: (a: f32, b: f32) => f32
max: (a: float, b: float) => float
max: (a: string, b: string) => string
max: (a: bool, b: bool) => bool
max: (a: bf16, b: bf16) => bf16
max: (a: f16, b: f16) => f16
clamp: (x: i32, lo: i32, hi: i32) => i32
clamp: (x: int, lo: int, hi: int) => int
clamp: (x: f32, lo: f32, hi: f32) => f32
clamp: (x: float, lo: float, hi: float) => float
sign: (x: i32) => i32
sign: (x: int) => int
sign: (x: f32) => f32
sign: (x: float) => float
is_nan: (x: f32) => bool
is_nan: (x: float) => bool
is_inf: (x: f32) => bool
is_inf: (x: float) => bool
gcd: (a: i32, b: i32) => i32
gcd: (a: int, b: int) => int
lcm: (a: i32, b: i32) => i32
lcm: (a: int, b: int) => int
bin: (x: i32) => string
bin: (x: int) => string
oct: (x: i32) => string
oct: (x: int) => string
hex: (x: i32) => string
hex: (x: int) => string
shl: (x: i32, count: i32) => i32
shl: (x: i32, count: int) => i32
shl: (x: int, count: i32) => int
shl: (x: int, count: int) => int
shr: (x: i32, count: i32) => i32
shr: (x: i32, count: int) => i32
shr: (x: int, count: i32) => int
shr: (x: int, count: int) => int
bit_and: (a: i32, b: i32) => i32
bit_and: (a: int, b: int) => int
bit_or: (a: i32, b: i32) => i32
bit_or: (a: int, b: int) => int
bit_xor: (a: i32, b: i32) => i32
bit_xor: (a: int, b: int) => int
bit_not: (x: i32) => i32
bit_not: (x: int) => int
```

**`statistics`** — import { ... } from statistics — 5 name(s)

```text
mean: (xs: i32[]) => float
mean: (xs: int[]) => float
mean: (xs: f32[]) => float
mean: (xs: float[]) => float
stdev: (xs: i32[]) => float
stdev: (xs: int[]) => float
stdev: (xs: f32[]) => float
stdev: (xs: float[]) => float
variance: (xs: i32[]) => float
variance: (xs: int[]) => float
variance: (xs: f32[]) => float
variance: (xs: float[]) => float
median: (xs: i32[]) => float?
median: (xs: int[]) => float?
median: (xs: f32[]) => float?
median: (xs: float[]) => float?
quantile: <q: float> (xs: i32[]) => float?
quantile: <q: float> (xs: int[]) => float?
quantile: <q: float> (xs: f32[]) => float?
quantile: <q: float> (xs: float[]) => float?
```

**`random`** — import { ... } from random — 10 name(s)

```text
seed: (value: int) => void
rand: () => float
randn: () => float
randint: (low: int, high: int) => int
choice: (xs: T[]) => T
sample: (xs: T[], n: int) => T[]
shuffle: (xs: T[]) => T[]
key: (seed: int) => { hi: int, lo: int }
split: (key: { hi: int, lo: int }) => ({ hi: int, lo: int }, { hi: int, lo: int })
permutation: (key: { hi: int, lo: int }, n: int) => int[]
```

**`time`** — import { ... } from time — 3 name(s)

```text
now: () => float
strftime: (timestamp: float, format: string) => string
strptime: (text: string, format: string) => float
```

**`fs`** — import { ... } from fs — 15 name(s)

```text
read_text: (path: string) => string
try_read_text: (path: string) => string!
read_chunk: (path: string, offset: int, size: int) => Bytes
read_bytes: (path: string) => Bytes
write_bytes: (path: string, content: Bytes) => void
replace: (path: string, content: Bytes) => void
write_text: (path: string, content: string) => void
exists: (path: string) => bool
is_file: (path: string) => bool
is_dir: (path: string) => bool
file_size: (path: string) => int
mkdir: (path: string) => bool
mkdirs: (path: string) => bool
remove: (path: string) => void!
remove_dir: (path: string) => void!
```

**`io`** — import { ... } from io — 5 name(s)

```text
set_terminal_raw_mode: (enabled: bool) => void
has_input: () => bool
get_char: () => string
get_chars: (max: int) => string
clear_input_buffer: () => void
```

**`process`** — import { ... } from process — 4 name(s)

```text
sleep: (milliseconds: int) => void
whoami: () => string
cpu_count: () => int
thread_count: () => int
```

**`env`** — import { ... } from env — 2 name(s)

```text
get_env: (name: string) => string
get_cwd: () => string
```

**`json`** — import { ... } from json — 14 name(s)

```text
encode: (value: T) => string
encode: (value: T, indent: int) => string
decode_as: <shape: T> (text: string) => T!
decode_as: <shape: T> (text: Json) => T!
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
```

**`hash`** — import { ... } from hash — 3 name(s)

```text
sha256: (s: string) => string
sha256: (s: Bytes) => string
sha256_rounds: (s: string, rounds: int) => string
```

**`re`** — import { ... } from re — 1 name(s)

```text
test: (pattern: string, text: string) => bool
```

**`para`** — import { ... } from para — 2 name(s)

```text
unordered_foreach: <f: (item: T) => void> (xs: T[]) => void
unordered_reduce: <f: (acc: T, item: T) => T, initial: T> (xs: T[]) => T
```

**`test_support`** — import { ... } from test_support — 2 name(s)

```text
rendezvous: (parties: int, timeout: float) => int uses rendezvous_point
thread_count: () => int uses process
```

**`this`** — import { ... } from this — 1 name(s)

```text
zen: () => string
```

**`graph`** — import { ... } from graph — 42 name(s)

```text
current: () => Program
graphs: (p: Program) => Graph[]
root: (p: Program) => Graph
name: (g: Graph) => string
nodes: (g: Graph) => Node[]
inputs: (n: Node) => Node[]
with_inputs: (n: Node) => Node[]
ctrl_inputs: (n: Node) => Node[]
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
origin: (n: Node) => int
constant: (n: Node, sample: i32) => i32?
constant: (n: Node, sample: int) => int?
constant: (n: Node, sample: f32) => f32?
constant: (n: Node, sample: float) => float?
constant: (n: Node, sample: bool) => bool?
constant: (n: Node, sample: string) => string?
region_of: (n: Node) => Region
region_kind: (r: Region) => RegionKind
region_parent: (r: Region) => Region?
show: (n: Node) => string
id: (h: Graph) => int
id: (h: Node) => int
id: (h: Op) => int
effects: (g: Graph) => Effects
shape: (n: Node) => string?
permissions: (n: Node) => int
program_of: (f: F) => Program
graph_of: (f: F) => Graph
open_function: (f: F) => Txn
to_function: (t: Txn, sample: F) => F!
open: (p: Program) => Txn
replace: (t: Txn, old: Node, new: Node) => void
erase: (t: Txn, n: Node) => void
eval: (t: Txn, n: Node) => Node?
commit: (t: Txn) => Program!
abort: (t: Txn) => void
add_constant: (t: Txn, g: Graph, value: i32) => Node
add_constant: (t: Txn, g: Graph, value: int) => Node
add_constant: (t: Txn, g: Graph, value: f32) => Node
add_constant: (t: Txn, g: Graph, value: float) => Node
add_constant: (t: Txn, g: Graph, value: bool) => Node
add_constant: (t: Txn, g: Graph, value: string) => Node
```

**`rewrite`** — import { ... } from rewrite — 22 name(s)

```text
rewrite: (p: Program, rules: Rule[]) => Program!
rewrite: (p: Program, rules: Rule[], strict: bool) => Program!
saturate: (p: Program, rules: Rule[], rounds: int) => Program!
saturate: (p: Program, rules: Rule[], rounds: int, strict: bool) => Program!
extract: (p: Program, cost: string) => Program!
check: (rules: Rule[], samples: int = 100, seed: int = 0, program: Program? = null) => CheckReport
check_text: (r: CheckReport) => string
check_row_count: (r: CheckReport) => int
check_rule: (r: CheckReport, row: int) => string!
check_instance: (r: CheckReport, row: int) => string!
check_verdict: (r: CheckReport, row: int) => string!
check_proposition: (r: CheckReport, row: int) => string!
check_evidence: (r: CheckReport, row: int) => string!
check_reason: (r: CheckReport, row: int) => string!
check_coverage: (r: CheckReport, row: int) => string!
check_coverage_table: () => string
run: <rules: Rule[], rounds: int = 16, strict: bool = false, explain: bool = false> (p: Program) => Run<Program>!
explain: (r: Run<Program>, a: Node, b: Node) => Proof!
explain_all: (r: Run<Program>) => Proof!
why_not: (r: Run<Program>, rule: Rule, v: Node) => string!
rule_name: (r: Rule) => string
rule_two_way: (r: Rule) => bool
rule_needs: (r: Rule) => int
rule_consts: (r: Rule) => int
```

**`tiles`** — import { ... } from tiles — 9 name(s)

```text
upload: (value: f32[..]) => Device<f32[..]>
download: (value: Device<f32[..]>) => f32[..]
run_preferred_resident: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => Device<f32>
source: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
inspect: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
benchmark: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
regions: <fn: (a: f32, b: f32) => f32> () => string
available: () => bool
device: () => string
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

**`python`** — import { ... } from python — 12 name(s)

```text
py_call: (fn: string | PyObject, ...args: PyObject[]) => PyObject
py_call: (fn: string | PyObject, args?: (...PyObject) | PyObject[], kwargs?: Struct<...PyObject>) => PyObject
py_print: (...objs: PyObject[]) => PyObject?
py_println: (...objs: PyObject[]) => PyObject?
py_wrap: (x: T) => PyObject<T>
py_unwrap: (obj: PyObject<T>) => T
py_exec: (code: string) => PyObject
py_eval: (expr: string) => PyObject
py_run: (file_path: string) => PyObject
py_attr: (obj: PyObject, attr: string) => PyObject
py_import: (module_name: string) => PyObject
py_dispose: (obj: PyObject) => void
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

**`autodiff`** — import { ... } from autodiff — 3 name(s)

```text
grad: (f: (x: float) => float) => (x: float) => float
with_grad: (f: (x: float) => float) => (x: float) => (float, float)
stop_gradient: (x: T) => T
```

**`sim`** — import { ... } from sim — 2 name(s)

```text
pi_hits: (samples: int, seed: int) => int
asian_call_price: (paths: int, steps: int, seed: int) => float
```

**`onnx`** — import { ... } from onnx — 2 name(s)

```text
export_model: (fn: (x: f32[..]) => void, example: f32[..], path: string) => void
supported_operators: () => string[]
```
<!-- camel-for-ai end: stdlib-signatures -->