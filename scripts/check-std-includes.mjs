#!/usr/bin/env node
// A source file that names something of the C++ standard library must get the header that
// declares it from its own include lines or from the project headers it includes, not from
// whatever one standard library happens to include inside another header. libstdc++, libc++
// and the MSVC library differ there, so a file that compiles on Linux only by that accident
// fails on macOS or Windows (`implicit instantiation of undefined template
// 'std::basic_ostringstream<char>'` and the like).
//
//   node scripts/check-std-includes.mjs            exit 1 and one line per missing header
//   node scripts/check-std-includes.mjs --fix      add the missing #include lines
//   node scripts/check-std-includes.mjs --direct   stricter report: the header must be named by
//                                                  the file itself (or, for x.cpp, by its x.h)
//   node scripts/check-std-includes.mjs <path>...  only these files or directories
//
// What is checked: every .h / .cpp under src/, include/, modules/ and tools/. For each file the
// standard headers it reaches are collected: its own `#include <...>` lines and those of the
// project headers it includes with `#include "..."`, transitively. Headers of third-party
// libraries count for nothing (what they include is theirs to change), and neither does an
// include inside a conditional on the platform (`#ifdef _WIN32`, ...). Then every use of a name
// in the table below (comments and string literals aside) needs one of the headers listed for
// it among those reached. The table also says which headers the standard, or every
// implementation in use, lets stand in for another (`<iostream>` for `<ostream>`, ...).
//
// The check reads text and does not parse C++: a name that is brought in by a macro, or a
// project type that shares its spelling with a standard one, is not seen or is seen wrongly.
// `// std-includes: ignore` on a line switches the check off for that line.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const SCAN_DIRS = ['src', 'include', 'modules', 'tools']
// Where `#include "a/b.h"` is looked up after the directory of the including file.
const INCLUDE_ROOTS = ['include', 'src', '.', 'third_party', 'modules', 'tools', 'tools/debugger']
// Sources that are not the project's own.
const SKIP = [/^third_party\//, /^modules\/python\/sdks\//, /\/third_party\//, /(^|\/)ui\//, /^(?:src|include\/camel)\/parse\/antlr\//]
const SOURCE = /\.(h|hpp|cpp|cc)$/

const PLATFORM_MACRO =
    /\b(_WIN32|_WIN64|_MSC_VER|__linux__|__APPLE__|__GLIBC__|__unix__|__MINGW32__|__CYGWIN__|__x86_64__|_M_X64|__aarch64__|_M_ARM64|PLATFORM_\w+)\b/

/** Headers that may be relied on to bring another one (the key provides the values). */
const IMPLIES = {
    iostream: ['ostream', 'istream', 'ios', 'streambuf', 'iosfwd'],
    sstream: ['ostream', 'istream', 'ios', 'string', 'iosfwd'],
    fstream: ['ostream', 'istream', 'ios', 'iosfwd'],
    ostream: ['ios', 'iosfwd'],
    istream: ['ostream', 'ios', 'iosfwd'],
    iomanip: [],
    string: ['string_view', 'initializer_list', 'compare'],
    string_view: ['compare'],
    vector: ['initializer_list', 'compare'],
    array: ['initializer_list', 'compare'],
    deque: ['initializer_list', 'compare'],
    list: ['initializer_list', 'compare'],
    map: ['initializer_list', 'compare'],
    set: ['initializer_list', 'compare'],
    unordered_map: ['initializer_list', 'compare'],
    unordered_set: ['initializer_list', 'compare'],
    queue: ['initializer_list', 'compare', 'deque', 'vector'],
    stack: ['initializer_list', 'compare', 'deque'],
    algorithm: ['initializer_list'],
    ranges: ['initializer_list', 'compare', 'iterator'],
    filesystem: ['compare', 'string', 'system_error'],
    system_error: ['stdexcept', 'string', 'exception', 'cerrno'],
    stdexcept: ['exception', 'string'],
    regex: ['string', 'initializer_list', 'compare'],
    thread: ['compare'],
    chrono: ['compare'],
    cinttypes: ['cstdint'],
    'stdint.h': ['cstdint'],
    'string.h': ['cstring'],
    'stdlib.h': ['cstdlib'],
    'stdio.h': ['cstdio'],
    'errno.h': ['cerrno'],
    'assert.h': ['cassert'],
    'math.h': ['cmath'],
    'ctype.h': ['cctype'],
    'stddef.h': ['cstddef'],
    'time.h': ['ctime'],
    'signal.h': ['csignal'],
    'limits.h': ['climits'],
}

const word = (names) => new RegExp(`\\bstd\\s*::\\s*(?:${names})\\b`)

/** [pattern, headers of which one is needed, what the report calls it]. */
const RULES = [
    [word('(?:basic_)?(?:o|i)?stringstream'), ['sstream'], 'std::stringstream'],
    [word('(?:basic_)?(?:o|i)?fstream'), ['fstream'], 'std::fstream'],
    [word('cout|cerr|cin|clog'), ['iostream'], 'std::cout'],
    [word('endl|flush|ostream'), ['ostream'], 'std::ostream'],
    [word('istream|getline'), ['istream', 'string'], 'std::istream'],
    [word('setw|setfill|setprecision|quoted|put_time|get_time'), ['iomanip'], 'std::setw'],
    [word('function|bind|bind_front|ref|cref|reference_wrapper|invoke|mem_fn|not_fn'), ['functional'], 'std::function'],
    [word('optional|nullopt|make_optional'), ['optional'], 'std::optional'],
    [word('variant|visit|monostate|get_if|holds_alternative'), ['variant'], 'std::variant'],
    [word('array|to_array'), ['array'], 'std::array'],
    [word('span|dynamic_extent|as_bytes'), ['span'], 'std::span'],
    [word('unordered_(?:multi)?map'), ['unordered_map'], 'std::unordered_map'],
    [word('unordered_(?:multi)?set'), ['unordered_set'], 'std::unordered_set'],
    [word('(?:multi)?map'), ['map'], 'std::map'],
    [word('(?:multi)?set'), ['set'], 'std::set'],
    [word('vector'), ['vector'], 'std::vector'],
    [word('deque'), ['deque'], 'std::deque'],
    [word('list|forward_list'), ['list', 'forward_list'], 'std::list'],
    [word('queue|priority_queue'), ['queue'], 'std::queue'],
    [word('stack'), ['stack'], 'std::stack'],
    [word('bitset'), ['bitset'], 'std::bitset'],
    [word('tuple|tie|make_tuple|apply|tuple_size|tuple_element|forward_as_tuple'), ['tuple'], 'std::tuple'],
    [word('(?:recursive_|timed_)?mutex|lock_guard|unique_lock|scoped_lock|once_flag|call_once'), ['mutex'], 'std::mutex'],
    [word('shared_(?:timed_)?mutex|shared_lock'), ['shared_mutex'], 'std::shared_mutex'],
    [word('atomic(?:_\\w+)?|memory_order(?:_\\w+)?'), ['atomic'], 'std::atomic'],
    [word('j?thread|this_thread'), ['thread'], 'std::thread'],
    [word('condition_variable(?:_any)?|cv_status'), ['condition_variable'], 'std::condition_variable'],
    [word('future|promise|async|packaged_task|launch'), ['future'], 'std::future'],
    [word('stop_token|stop_source|stop_callback'), ['stop_token'], 'std::stop_token'],
    [word('chrono'), ['chrono'], 'std::chrono'],
    [word('mem(?:cpy|move|set|cmp|chr)|str(?:len|cmp|ncmp|chr|rchr|str|cpy|ncpy|cat|error)'), ['cstring'], 'std::memcpy'],
    [word('malloc|calloc|realloc|free|strto(?:d|f|l|ll|ul|ull|ld)|getenv|abort|exit|_Exit|atexit|quick_exit|ato(?:i|l|ll|f)|qsort|system|aligned_alloc'), ['cstdlib'], 'std::malloc'],
    [/(?<![\w:.>])errno\b/, ['cerrno'], 'errno'],
    [word('numeric_limits'), ['limits'], 'std::numeric_limits'],
    [word('iota|accumulate|reduce|inner_product|partial_sum|adjacent_difference|gcd|lcm|midpoint|transform_reduce'), ['numeric'], 'std::accumulate'],
    [
        word(
            '(?:stable_|partial_)?sort|find(?:_if|_if_not|_first_of|_end)?|count(?:_if)?|min|max|minmax|clamp|(?:min|max|minmax)_element|copy(?:_if|_n|_backward)?|fill(?:_n)?|transform|for_each(?:_n)?|all_of|any_of|none_of|reverse|rotate|unique|remove(?:_if)?|replace(?:_if)?|equal|mismatch|lexicographical_compare|(?:lower|upper)_bound|binary_search|equal_range|nth_element|is_sorted|merge|includes|set_(?:union|intersection|difference)|swap_ranges|search|generate(?:_n)?|shuffle|next_permutation|prev_permutation|partition|stable_partition|adjacent_find|is_permutation|(?:push|pop|make)_heap',
        ),
        ['algorithm'],
        'std::sort',
    ],
    [word('ranges\\s*::\\s*(?!views\\b|range\\b|view\\b|begin\\b|end\\b|size\\b|subrange\\b|to\\b|\\w+_range\\b|range_\\w+\\b|iterator_t\\b|sentinel_t\\b|empty\\b|data\\b|ssize\\b)\\w+'), ['algorithm'], 'std::ranges algorithm'],
    [word('views|ranges\\s*::\\s*(?:views|range|view|subrange|to|\\w+_range|range_\\w+|iterator_t|sentinel_t)'), ['ranges'], 'std::ranges / std::views'],
    [word('(?:unique|shared|weak)_ptr|make_(?:unique|shared)(?:_for_overwrite)?|enable_shared_from_this|allocator(?:_traits)?|addressof|construct_at|destroy_at|uninitialized_\\w+|(?:static|dynamic|const|reinterpret)_pointer_cast|default_delete'), ['memory'], 'std::unique_ptr'],
    [word('(?:basic_)?string_view'), ['string_view'], 'std::string_view'],
    [word('(?:basic_|u8|u16|u32|w)?string|to_string|sto(?:i|l|ll|ul|ull|f|d|ld)|char_traits'), ['string'], 'std::string'],
    [word('v?format(?:_to(?:_n)?|ted_size)?|formatter|format_error|make_format_args|format_context|format_parse_context'), ['format'], 'std::format'],
    [word('print|println'), ['print'], 'std::print'],
    [word('filesystem'), ['filesystem'], 'std::filesystem'],
    [word('bit_cast|popcount|countl_zero|countl_one|countr_zero|countr_one|bit_width|bit_ceil|bit_floor|has_single_bit|rotl|rotr|endian|byteswap'), ['bit'], 'std::bit_cast'],
    [word('exchange|pair|make_pair|piecewise_construct|in_place(?:_t|_type|_index)?(?:_t)?|declval|as_const|unreachable|to_underlying|cmp_(?:less|greater|equal|not_equal|less_equal|greater_equal)|in_range|index_sequence(?:_for)?|make_index_sequence|integer_sequence'), ['utility', 'map', 'unordered_map'], 'std::exchange / std::pair'],
    [/(?<![\w:.>"])assert\s*\(/, ['cassert'], 'assert'],
    [/(?<![\w.>"])(?:std\s*::\s*)?u?int(?:_fast|_least)?(?:8|16|32|64)_t\b|(?<![\w.>"])(?:std\s*::\s*)?u?int(?:ptr|max)_t\b|\bU?INT(?:8|16|32|64|PTR)_(?:MAX|MIN)\b|\bSIZE_MAX\b/, ['cstdint'], 'uint64_t and the like'],
    [word('byte|nullptr_t|max_align_t|ptrdiff_t'), ['cstddef'], 'std::byte'],
    [word('runtime_error|logic_error|invalid_argument|out_of_range|domain_error|length_error|range_error|overflow_error|underflow_error'), ['stdexcept'], 'std::runtime_error'],
    [word('exception(?:_ptr)?|current_exception|rethrow_exception|make_exception_ptr|terminate|set_terminate|uncaught_exceptions|nested_exception|throw_with_nested|bad_exception'), ['exception'], 'std::exception'],
    [word('bad_alloc|nothrow|launder|align_val_t|hardware_(?:de|con)structive_interference_size|bad_array_new_length'), ['new'], 'std::bad_alloc'],
    [word('error_code|error_condition|system_error|errc|generic_category|system_category|make_error_code|error_category'), ['system_error'], 'std::error_code'],
    [word('type_index'), ['typeindex'], 'std::type_index'],
    [word('type_info|bad_cast|bad_typeid'), ['typeinfo'], 'std::type_info'],
    [word('any|any_cast|make_any|bad_any_cast'), ['any'], 'std::any'],
    [word('from_chars|to_chars|chars_format|from_chars_result|to_chars_result'), ['charconv'], 'std::from_chars'],
    [word('sqrt|cbrt|pow|exp|exp2|expm1|log|log2|log10|log1p|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|floor|ceil|round|lround|llround|trunc|fmod|remainder|fabs|hypot|isnan|isinf|isfinite|signbit|copysign|nextafter|fmin|fmax|erf|erfc|tgamma|lgamma|ldexp|frexp|modf|nearbyint|rint|fma'), ['cmath'], 'std::sqrt'],
    [word('abs'), ['cmath', 'cstdlib'], 'std::abs'],
    [word('is(?:digit|alpha|alnum|space|upper|lower|punct|xdigit|print|cntrl|graph|blank)|to(?:upper|lower)'), ['cctype', 'locale'], 'std::isdigit'],
    [word('s?n?printf|fprintf|vs?n?printf|vfprintf|FILE|fopen|fclose|fread|fwrite|fflush|fputs|fputc|fgets|fgetc|puts|putchar|getchar|perror|fseek|ftell|rewind|feof|ferror|setvbuf|tmpfile|sscanf|fscanf|scanf'), ['cstdio'], 'std::printf'],
    [word('initializer_list'), ['initializer_list'], 'std::initializer_list'],
    [word('source_location'), ['source_location'], 'std::source_location'],
    [word('expected|unexpected|unexpect|bad_expected_access'), ['expected'], 'std::expected'],
    [word('(?:is|remove|add|make|has|common|underlying|conditional|enable_if|decay|invoke_result|void_t|true_type|false_type|integral_constant|bool_constant|conjunction|disjunction|negation|type_identity|aligned_storage|alignment_of|rank|extent)(?:_\\w+)?(?<!is_sorted|is_permutation|is_partitioned|is_heap|make_pair|make_tuple|make_unique|make_shared|make_optional|make_any|make_heap|make_error_code|make_format_args|make_exception_ptr|make_index_sequence|has_single_bit|remove_if|remove_copy|remove_copy_if|is_eq|is_neq|is_lt|is_gt|is_lteq|is_gteq)\\b(?=\\s*[<:{(]|\\s*$)'), ['type_traits'], 'type traits'],
    [word('same_as|derived_from|convertible_to|integral|signed_integral|unsigned_integral|floating_point|invocable|regular_invocable|predicate|constructible_from|default_initializable|move_constructible|copy_constructible|movable|copyable|semiregular|regular|equality_comparable|totally_ordered|assignable_from|swappable|common_with|common_reference_with|destructible'), ['concepts'], 'concepts'],
    [word('(?:strong|weak|partial)_ordering|compare_three_way|three_way_comparable(?:_with)?|is_(?:eq|neq|lt|gt|lteq|gteq)'), ['compare'], 'std::strong_ordering'],
    [word('back_inserter|front_inserter|inserter|distance|next|prev|advance|(?:reverse|move|istream|ostream|istreambuf|ostreambuf|back_insert|insert)_iterator|make_(?:reverse|move)_iterator|iterator_traits|(?:input|output|forward|bidirectional|random_access|contiguous)_iterator(?:_tag)?|ssize|c?r?begin|c?r?end|size|empty|data|default_sentinel(?:_t)?|sentinel_for|iter_\\w+_t|indirectly_\\w+'), ['iterator', 'vector', 'string', 'array', 'map', 'set', 'unordered_map', 'unordered_set', 'deque', 'list', 'span', 'string_view', 'ranges'], 'iterator utilities'],
    [word('signal|raise|sig_atomic_t'), ['csignal'], 'std::signal'],
    [word('time|time_t|tm|localtime|gmtime|mktime|strftime|clock|clock_t|difftime|asctime|ctime|timespec'), ['ctime', 'chrono'], 'std::time'],
    [word('mt19937(?:_64)?|random_device|(?:uniform|normal|bernoulli|poisson|exponential|discrete|binomial|geometric)_\\w*distribution|default_random_engine|minstd_rand0?|seed_seq'), ['random'], 'std::mt19937'],
    [word('regex|smatch|cmatch|sregex_(?:token_)?iterator|regex_(?:match|search|replace|error|constants)|ssub_match'), ['regex'], 'std::regex'],
    [word('numbers'), ['numbers'], 'std::numbers'],
    [word('complex'), ['complex'], 'std::complex'],
    [word('stacktrace(?:_entry)?'), ['stacktrace'], 'std::stacktrace'],
    [word('execution'), ['execution'], 'std::execution'],
    [word('counting_semaphore|binary_semaphore'), ['semaphore'], 'std::counting_semaphore'],
    [word('latch'), ['latch'], 'std::latch'],
    [word('barrier'), ['barrier'], 'std::barrier'],
    [word('locale|use_facet|numpunct|ctype'), ['locale'], 'std::locale'],
    [word('setlocale|lconv|localeconv'), ['clocale'], 'std::setlocale'],
    [word('va_list'), ['cstdarg'], 'std::va_list'],
    [word('feclearexcept|fetestexcept|fe[gs]etround|fenv_t'), ['cfenv'], 'floating-point environment'],
    [word('lock'), ['mutex'], 'std::lock'],
    [word('hash'), ['functional', 'string', 'string_view', 'memory', 'optional', 'variant', 'typeindex', 'thread', 'bitset', 'system_error', 'vector', 'unordered_map', 'unordered_set'], 'std::hash'],
    [word('less|greater|equal_to|not_equal_to|less_equal|greater_equal|plus|minus|multiplies|divides|modulus|negate|logical_(?:and|or|not)|bit_(?:and|or|xor|not)|identity'), ['functional'], 'function objects'],
]

function rel(file) {
    return path.relative(ROOT, file).split(path.sep).join('/')
}

function walk(entry, out) {
    const stat = fs.statSync(entry)
    if (stat.isFile()) {
        if (SOURCE.test(entry) && !SKIP.some((p) => p.test(rel(entry)))) out.push(entry)
        return
    }
    for (const name of fs.readdirSync(entry).sort()) {
        if (name === 'node_modules' || name === 'build' || name.startsWith('.')) continue
        const child = path.join(entry, name)
        if (SKIP.some((p) => p.test(rel(child) + '/'))) continue
        walk(child, out)
    }
}

/** The text with comments, string and character literals blanked (line structure kept). */
function codeOnly(text) {
    let out = ''
    let i = 0
    const n = text.length
    const blank = (s) => s.replace(/[^\n]/g, ' ')
    while (i < n) {
        const c = text[i]
        const d = text[i + 1]
        if (c === '/' && d === '/') {
            const end = text.indexOf('\n', i)
            const stop = end < 0 ? n : end
            out += blank(text.slice(i, stop))
            i = stop
        } else if (c === '/' && d === '*') {
            const end = text.indexOf('*/', i + 2)
            const stop = end < 0 ? n : end + 2
            out += blank(text.slice(i, stop))
            i = stop
        } else if (c === 'R' && d === '"' && !/\w/.test(text[i - 1] || '')) {
            const open = text.indexOf('(', i)
            const delimiter = text.slice(i + 2, open)
            const end = text.indexOf(`)${delimiter}"`, open)
            const stop = end < 0 ? n : end + delimiter.length + 2
            out += '""' + blank(text.slice(i + 2, stop))
            i = stop
        } else if (c === '"' || (c === "'" && !/[0-9a-fA-F]/.test(text[i - 1] || ''))) {
            let j = i + 1
            while (j < n && text[j] !== c && text[j] !== '\n') j += text[j] === '\\' ? 2 : 1
            out += c + blank(text.slice(i + 1, j)) + (text[j] === c ? c : '')
            i = text[j] === c ? j + 1 : j
        } else {
            out += c
            i++
        }
    }
    return out
}

const cache = new Map()

/**
 * What one file says: its includes (those inside a conditional on the platform marked), and the
 * rules its code uses with the first line of each.
 */
function read(file) {
    if (cache.has(file)) return cache.get(file)
    const text = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')
    const rawLines = text.split('\n')
    const lines = codeOnly(text).split('\n')
    const info = { file, std: new Set(), project: [], uses: new Map(), rawLines }
    cache.set(file, info)
    const conditions = [] // true: the block depends on the platform
    lines.forEach((line, index) => {
        const raw = rawLines[index]
        const directive = /^\s*#\s*(\w+)\s*(.*)$/.exec(line)
        if (directive) {
            const [, name] = directive
            const rest = /^\s*#\s*\w+\s*(.*)$/.exec(raw)[1]
            if (name === 'if' || name === 'ifdef' || name === 'ifndef') {
                conditions.push(PLATFORM_MACRO.test(rest))
            } else if (name === 'elif' || name === 'elifdef' || name === 'elifndef') {
                if (conditions.length > 0) {
                    conditions[conditions.length - 1] ||= PLATFORM_MACRO.test(rest)
                }
            } else if (name === 'endif') {
                conditions.pop()
            } else if (name === 'include') {
                const platformOnly = conditions.some(Boolean)
                const angle = /^<([^>]+)>/.exec(rest)
                const quote = /^"([^"]+)"/.exec(rest)
                if (angle && !platformOnly) info.std.add(angle[1])
                if (quote && !platformOnly) {
                    const target = resolveInclude(file, quote[1])
                    if (target) info.project.push(target)
                }
            }
            return
        }
        // Code for one platform gets its headers from an include for that platform.
        if (raw.includes('std-includes: ignore') || conditions.some(Boolean)) return
        for (let r = 0; r < RULES.length; r++) {
            if (!info.uses.has(r) && RULES[r][0].test(line)) info.uses.set(r, index + 1)
        }
    })
    return info
}

function resolveInclude(from, name) {
    const candidates = [path.resolve(path.dirname(from), name)]
    for (const root of INCLUDE_ROOTS) candidates.push(path.resolve(ROOT, root, name))
    // A module names its own headers from its directory (modules/<name>/...).
    const module = /^(modules\/[^/]+|tools\/[^/]+)\//.exec(rel(from))
    if (module) candidates.push(path.resolve(ROOT, module[1], name))
    for (const candidate of candidates) {
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
            const r = rel(candidate)
            if (r.startsWith('..') || SKIP.some((p) => p.test(r))) return null
            return candidate
        }
    }
    return null
}

function withImplied(headers) {
    const all = new Set(headers)
    const queue = [...headers]
    while (queue.length > 0) {
        for (const more of IMPLIES[queue.pop()] || []) {
            if (!all.has(more)) {
                all.add(more)
                queue.push(more)
            }
        }
    }
    return all
}

/** The standard headers a file reaches: its own and those of the project headers it includes. */
function reached(file, direct) {
    const info = read(file)
    if (direct) {
        const own = new Set(info.std)
        const paired = file.replace(/\.(cpp|cc)$/, '.h')
        for (const header of info.project) {
            if (path.basename(header) === path.basename(paired)) {
                for (const h of read(header).std) own.add(h)
            }
        }
        return withImplied(own)
    }
    const seen = new Set([file])
    const all = new Set()
    const stack = [file]
    while (stack.length > 0) {
        const current = read(stack.pop())
        for (const h of current.std) all.add(h)
        for (const next of current.project) {
            if (!seen.has(next)) {
                seen.add(next)
                stack.push(next)
            }
        }
    }
    return withImplied(all)
}

/** Adds `#include <header>` lines to a file, in the block of its standard includes. */
function addIncludes(file, headers) {
    const text = fs.readFileSync(file, 'utf8')
    const eol = text.includes('\r\n') ? '\r\n' : '\n'
    const lines = text.split(eol)
    const code = codeOnly(lines.join('\n')).split('\n')
    let depth = 0
    let lastAngle = -1
    let lastQuote = -1
    let pragma = -1
    for (let i = 0; i < code.length; i++) {
        const directive = /^\s*#\s*(\w+)/.exec(code[i])
        if (!directive) continue
        const name = directive[1]
        if (name === 'if' || name === 'ifdef' || name === 'ifndef') depth++
        else if (name === 'endif') depth--
        else if (name === 'pragma' && /once/.test(lines[i])) pragma = i
        else if (name === 'include' && depth === 0) {
            if (/#\s*include\s*</.test(lines[i])) lastAngle = i
            else lastQuote = i
        }
    }
    const added = headers.map((h) => `#include <${h}>`)
    if (lastAngle >= 0) {
        // Into the block the last standard include stands in, keeping the block sorted.
        let first = lastAngle
        while (first > 0 && /^\s*#\s*include\s*</.test(lines[first - 1])) first--
        const block = [...lines.slice(first, lastAngle + 1), ...added]
        const sorted = [...new Set(block)].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0))
        lines.splice(first, lastAngle + 1 - first, ...sorted)
    } else if (lastQuote >= 0) {
        lines.splice(lastQuote + 1, 0, '', ...added)
    } else {
        const at = pragma >= 0 ? pragma + 1 : 0
        lines.splice(at, 0, '', ...added)
    }
    fs.writeFileSync(file, lines.join(eol))
}

function main() {
    const args = process.argv.slice(2)
    const fix = args.includes('--fix')
    const direct = args.includes('--direct')
    const targets = args.filter((a) => !a.startsWith('--'))
    const files = []
    for (const target of targets.length > 0 ? targets : SCAN_DIRS) {
        const entry = path.resolve(ROOT, target)
        if (!fs.existsSync(entry)) {
            console.error(`no such file or directory: ${target}`)
            process.exit(2)
        }
        walk(entry, files)
    }
    let findings = 0
    let filesWithFindings = 0
    for (const file of files) {
        const info = read(file)
        if (info.uses.size === 0) continue
        const have = reached(file, direct)
        const missing = new Map() // header -> [what, line]
        for (const [rule, line] of info.uses) {
            const [, headers, what] = RULES[rule]
            if (headers.some((h) => have.has(h))) continue
            if (!missing.has(headers[0])) missing.set(headers[0], [what, line])
        }
        if (missing.size === 0) continue
        filesWithFindings++
        for (const [header, [what, line]] of missing) {
            findings++
            console.log(`${rel(file)}:${line}: uses ${what} without <${header}>`)
        }
        if (fix) addIncludes(file, [...missing.keys()])
    }
    const mode = direct ? 'named by the file itself' : 'reached through its includes'
    if (findings > 0) {
        console.log(
            `std includes: ${findings} missing header(s) in ${filesWithFindings} of ${files.length} files (${mode})` +
                (fix ? '; added' : '; run with --fix to add them'),
        )
        process.exit(fix ? 0 : 1)
    }
    console.log(`std includes: ${files.length} files, every standard header used is ${mode}`)
}

main()
