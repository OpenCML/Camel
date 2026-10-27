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
**`Built-in functions`** — no import — 55 name(s)

```text
std::mem::blocks: () => { allocs: int, frees: int, live: int, bytes: int }
int: (x: S) => int when is_real::<S>() || is_string::<S>()
i64: (x: S) => int when is_real::<S>() || is_string::<S>()
i32: (x: S) => i32 when is_real::<S>() || is_string::<S>()
float: (x: S) => float when is_real::<S>() || is_string::<S>()
f64: (x: S) => float when is_real::<S>() || is_string::<S>()
f32: (x: S) => f32 when is_real::<S>() || is_string::<S>()
bf16: (x: S) => bf16 when is_real::<S>() || is_string::<S>()
f16: (x: S) => f16 when is_real::<S>() || is_string::<S>()
c64: (x: S) => c64 when is_real::<S>() || is_complex::<S>()
c128: (x: S) => c128 when is_real::<S>() || is_complex::<S>()
string: (x: T) => string
parse_int: (text: string) => int!
parse_float: (text: string) => float!
error: (kind: string, message: string) => Error
print: (x: T, ...rest: U...) => T
println: (x: T, ...rest: U...) => T
eprintln: (x: T, ...rest: U...) => T
format: <...args: T...> (fmt: string) => string
join: <sep: string> (xs: string[]) => string
len: (x: string) => int
len: (x: T[..]) => int
len: (x: Bytes) => int
range: (start: int, end: int, step?: int) => int[]
last: (xs: T[]) => T
slice: <axis: int, start: int, end: int, step: int = 1> (x: T[]) => T[]
slice: <axis: int, start: int, end: int, step: int = 1> (x: string) => string
slice: <axis: int, start: int, end: int, step: int = 1> (x: Bytes) => Bytes
concat: (xs: T[], ys: T[]) => T[]
contains: <value: T> (xs: T[]) => bool
contains: <sub: string> (s: string) => bool
zip: (xs: T[], ys: U[]) => (T, U)[]
append: <value: T> (xs: T[]) => T[]
map: <f: (item: T) => U> (xs: T[]) => U[]
filter: <f: (item: T) => bool> (xs: T[]) => T[]
reduce: <f: (acc: U, item: T) => U, initial: U> (xs: T[]) => U
foreach: <f: (item: T) => void> (xs: T[]) => void
fail: <Kind: string> (message: string) => never
attempt: (f: (...args: A) => T) => (...args: A) => T!
len: (x: Map<K, V>) => int
len: (x: Set<T>) => int
get: <key: K> (m: Map<K, V>) => V?
get_or: <key: K, fallback: V> (m: Map<K, V>) => V
put: <key: K, value: V> (m: Map<K, V>) => Map<K, V>
remove: <key: K> (c: Map<K, V>) => Map<K, V>
remove: (c: Set<K>) => Set<K>
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

**`tensor`** — import { ... } from tensor — 76 name(s)

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
i32: (t: float[..] | number[]) => i32[..]
i64: (t: float[..] | number[]) => int64[..]
int: (t: float[..] | number[]) => int64[..]
f32: (t: float[..] | number[]) => f32[..]
f64: (t: float[..] | number[]) => f64[..]
float: (t: float[..] | number[]) => f64[..]
bf16: (t: float[..] | number[]) => bf16[..]
f16: (t: float[..] | number[]) => f16[..]
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

**`array`** — import { ... } from array — 35 name(s)

```text
reverse: (xs: T[]) => T[]
take: <n: int> (xs: T[]) => T[]
drop: <n: int> (xs: T[]) => T[]
chunks: <n: int> (xs: T[]) => T[][]
enumerate: (xs: T[]) => (int, T)[]
flatten: (xss: T[][]) => T[]
flatten: (xs: T[..]) => T[] when !is_array_type::<T>()
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
sort_by: <key: (x: T) => K> (xs: T[]) => T[] when is_ordered::<K>()
flat_map: <f: (x: T) => U[]> (xs: T[]) => U[]
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
encode: (value: T) => string
encode: <indent: int?> (value: T) => string
decode_as: (source: string) => T!
decode_as: (source: Json) => T!
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

**`python`** — import { ... } from python — 12 name(s)

```text
py_call: (fn: string | PyObject, ...args: PyObject[]) => PyObject!
py_call: (fn: string | PyObject, args?: (...PyObject) | PyObject[], kwargs?: Struct<...PyObject>) => PyObject!
py_print: (...objs: PyObject[]) => PyObject?
py_println: (...objs: PyObject[]) => PyObject?
py_wrap: (x: T) => PyObject<T>
py_unwrap: (obj: PyObject<T>) => T!
py_exec: (code: string) => PyObject!
py_eval: (expr: string) => PyObject!
py_run: (file_path: string) => PyObject!
py_attr: (obj: PyObject, attr: string) => PyObject!
py_import: (module_name: string) => PyObject!
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
grad: (f: F) => Gradient<F> when is_function::<F>()
with_grad: (f: F) => ValueAndGradient<F> when is_function::<F>()
stop_gradient: (x: T) => T pure
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