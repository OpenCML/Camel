// Prototype: the open-call count passed as a hidden argument instead of a global counter.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
namespace rt {
inline int64_t add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
inline int64_t sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
inline uint32_t limit = 1000;
[[noreturn, gnu::cold, gnu::noinline]] inline void too_deep() { std::fputs("too deep\n", stdout); std::exit(1); }
}
static int64_t fib(uint32_t depth, int64_t n) {
    if (depth > rt::limit) [[unlikely]] rt::too_deep();
    if (n <= 1) return n;
    return rt::add(fib(depth + 1, rt::sub(n, 1)), fib(depth + 1, rt::sub(n, 2)));
}
int main() { std::printf("%lld\n", (long long)fib(1, 37)); return 0; }
