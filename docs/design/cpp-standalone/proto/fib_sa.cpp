// Prototype: fib(37) in the proposed standalone style. Variants by macro:
//   GUARD=0 no recursion counter; GUARD=1 counter in the callee prologue (RAII).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
namespace rt {
inline int64_t add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
inline int64_t sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
inline size_t open_calls = 0, limit = 1000;
[[noreturn, gnu::cold, gnu::noinline]] inline void too_deep() { std::fputs("too deep\n", stdout); std::exit(1); }
struct CallGuard {
    CallGuard() { if (++open_calls > limit) [[unlikely]] too_deep(); }
    ~CallGuard() { --open_calls; }
};
}
static int64_t fib(int64_t n) {
#if GUARD
    rt::CallGuard guard;
#endif
    if (n <= 1) return n;
    return rt::add(fib(rt::sub(n, 1)), fib(rt::sub(n, 2)));
}
int main() { std::printf("%lld\n", (long long)fib(37)); return 0; }
