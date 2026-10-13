// Hand-written: the two workloads of roots.cpp with a memory model of the emitted code's own,
// no Camel runtime linked. NOT emitter output; a yardstick for design option 2.
//
//   rc <workload> <mode> [n]        workload: loop | calls      mode: rc | value
//
// rc     an intrusive reference count on a malloc'ed string (non-atomic: one thread)
// value  std::string held by value, moved on its last use
//
// Build: clang++ -std=c++23 -O2 docs/design/cpp-memory/examples/handwritten/rc.cpp -o /tmp/cppmem/rc
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

struct RcString {
    long rc;
    unsigned size;
    char data[1];
};

static RcString *rcMake(long long i) {
    char buf[32];
    const int len = std::snprintf(buf, sizeof buf, "item-%lld", i);
    auto *s       = static_cast<RcString *>(std::malloc(sizeof(RcString) + len));
    s->rc         = 1;
    s->size       = static_cast<unsigned>(len);
    std::memcpy(s->data, buf, len + 1);
    return s;
}
static inline void rcDrop(RcString *s) {
    if (--s->rc == 0) {
        std::free(s);
    }
}

[[gnu::noinline]] static long long loopRc(long long n) {
    RcString *last = rcMake(0);
    last->size     = 0;
    long long acc  = 0;
    for (long long i = n; i > 0; --i) {
        acc += last->size;
        RcString *next = rcMake(i);
        rcDrop(last); // last use of the old value
        last = next;
    }
    acc += last->size;
    rcDrop(last);
    return acc;
}

[[gnu::noinline]] static long long depthRc(long long d) {
    if (d == 0) {
        return 0;
    }
    RcString *s           = rcMake(d);
    const long long below = depthRc(d - 1);
    const long long r     = below + s->size;
    rcDrop(s);
    return r;
}

static std::string valueMake(long long i) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "item-%lld", i);
    return std::string(buf);
}

[[gnu::noinline]] static long long loopValue(long long n) {
    std::string last;
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(last.size());
        last = valueMake(i);
    }
    return acc + static_cast<long long>(last.size());
}

[[gnu::noinline]] static long long depthValue(long long d) {
    if (d == 0) {
        return 0;
    }
    const std::string s   = valueMake(d);
    const long long below = depthValue(d - 1);
    return below + static_cast<long long>(s.size());
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: rc loop|calls rc|value [n]\n";
        return 2;
    }
    const std::string workload = argv[1];
    const bool rc              = std::string(argv[2]) == "rc";
    const long long n          = argc > 3 ? std::atoll(argv[3]) : 2000000;
    long long result           = 0;
    if (workload == "loop") {
        result = rc ? loopRc(n) : loopValue(n);
    } else {
        for (long long r = 0; r < n / 500; ++r) {
            result += rc ? depthRc(500) : depthValue(500);
        }
    }
    std::cout << result << std::endl;
    return 0;
}
