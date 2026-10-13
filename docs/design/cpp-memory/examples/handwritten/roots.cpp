// Hand-written C++ in the shape std::cpp emits (raw `String *` in C++ locals, objects in the
// runtime's auto space), plus the two things the emitter does not do today: reach a safepoint,
// and tell the collector where its references are. NOT emitter output.
//
//   roots <workload> <mode> [n]
//
// workload  loop    one string alive across two million iterations (a04_string_churn as a loop)
//           calls   a recursion 500 deep, repeated n/500 times; every activation holds one string
//                   across the call below it
//           keep    one string made at the start and read after n other strings have been made
// mode      raw          today's emitted code: raw pointer, no safepoint is ever reached
//           raw-poll     raw pointer, and a safepoint poll where a Camel call would be
//           shadow       the pointer is registered in a shadow stack; poll
//           handle       the pointer is held in a mm::RootHandle; poll
//
// Build: bash docs/design/cpp-memory/tools/cppbuild.sh docs/design/cpp-memory/examples/handwritten/roots.cpp
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type.h"
#include "camel/passes/trans/cpp/cpp_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace mm = camel::core::mm;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::execute::cppbridge;

// ---------------------------------------------------------------------------------------------
// A shadow stack: one tracer registered with the collector; an entry is the address of a C++
// local that holds a reference, and the static type the collector needs to trace through it.
// ---------------------------------------------------------------------------------------------
struct ShadowEntry {
    Object **loc;
    const Type *type;
};
static ShadowEntry g_shadow[1 << 16];
static size_t g_shadowTop = 0;

struct ShadowFrame {
    size_t saved;
    ShadowFrame() : saved(g_shadowTop) {}
    ~ShadowFrame() { g_shadowTop = saved; }
    template <typename T> void root(T *&local, const Type *type) {
        g_shadow[g_shadowTop++] = ShadowEntry{reinterpret_cast<Object **>(&local), type};
    }
};

static void installShadowStack() {
    mm::autoSpace().registerExternalRootTracer(
        &g_shadow,
        "cpp.shadowStack",
        [](const mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
            for (size_t i = 0; i < g_shadowTop; ++i) {
                Object *&ref = *g_shadow[i].loc;
                if (ref) {
                    ref = relocate(
                        ref,
                        g_shadow[i].type,
                        RefTraceInfo{
                            .owner     = nullptr,
                            .ownerType = nullptr,
                            .slotType  = g_shadow[i].type,
                            .ownerKind = "cpp.shadowStack",
                            .slotName  = {},
                            .slotIndex = i,
                        });
                }
            }
        });
}

enum class Mode { Raw, RawPoll, Shadow, Handle };

static String *makeItem(long long i) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "item-%lld", i);
    return String::from(buf, mm::autoSpace());
}

// ---- workload "loop" ------------------------------------------------------------------------
[[gnu::noinline]] static long long loopRaw(long long n, bool poll) {
    String *last  = String::from("", mm::autoSpace());
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(last->size());
        last = makeItem(i);
        if (poll) {
            mm::safepoint("where a call of Camel code would be");
        }
    }
    return acc + static_cast<long long>(last->size());
}

[[gnu::noinline]] static long long loopShadow(long long n) {
    String *last = String::from("", mm::autoSpace());
    ShadowFrame frame;
    frame.root(last, Type::String());
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(last->size());
        last = makeItem(i);
        mm::safepoint("where a call of Camel code would be");
    }
    return acc + static_cast<long long>(last->size());
}

[[gnu::noinline]] static long long loopHandle(long long n) {
    mm::RootHandle last(mm::autoSpace(), String::from("", mm::autoSpace()), Type::String(), "last");
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(last.getAs<String>()->size());
        last.reset(makeItem(i), Type::String());
        mm::safepoint("where a call of Camel code would be");
    }
    return acc + static_cast<long long>(last.getAs<String>()->size());
}

// ---- workload "calls" -----------------------------------------------------------------------
// depth(d) = len(s) + depth(d - 1), where s is a string made in this activation and read after
// the call returns.
[[gnu::noinline]] static long long depthRaw(long long d, bool poll) {
    if (d == 0) {
        return 0;
    }
    String *s = makeItem(d);
    if (poll) {
        mm::safepoint("function entry");
    }
    const long long below = depthRaw(d - 1, poll);
    return below + static_cast<long long>(s->size());
}

[[gnu::noinline]] static long long depthShadow(long long d) {
    if (d == 0) {
        return 0;
    }
    String *s = makeItem(d);
    ShadowFrame frame;
    frame.root(s, Type::String());
    mm::safepoint("function entry");
    const long long below = depthShadow(d - 1);
    return below + static_cast<long long>(s->size());
}

[[gnu::noinline]] static long long depthHandle(long long d) {
    if (d == 0) {
        return 0;
    }
    mm::RootHandle s(mm::autoSpace(), makeItem(d), Type::String(), "s");
    mm::safepoint("function entry");
    const long long below = depthHandle(d - 1);
    return below + static_cast<long long>(s.getAs<String>()->size());
}

// ---- workload "keep" ------------------------------------------------------------------------
[[gnu::noinline]] static std::string keepRaw(long long n, bool poll) {
    String *first = String::from("the string kept from the start", mm::autoSpace());
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(makeItem(i)->size());
        if (poll) {
            mm::safepoint("where a call of Camel code would be");
        }
    }
    return first->toString() + " / " + std::to_string(acc);
}

[[gnu::noinline]] static std::string keepShadow(long long n) {
    String *first = String::from("the string kept from the start", mm::autoSpace());
    ShadowFrame frame;
    frame.root(first, Type::String());
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(makeItem(i)->size());
        mm::safepoint("where a call of Camel code would be");
    }
    return first->toString() + " / " + std::to_string(acc);
}

[[gnu::noinline]] static std::string keepHandle(long long n) {
    mm::RootHandle first(
        mm::autoSpace(),
        String::from("the string kept from the start", mm::autoSpace()),
        Type::String(),
        "first");
    long long acc = 0;
    for (long long i = n; i > 0; --i) {
        acc += static_cast<long long>(makeItem(i)->size());
        mm::safepoint("where a call of Camel code would be");
    }
    return first.getAs<String>()->toString() + " / " + std::to_string(acc);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: roots loop|calls|keep raw|raw-poll|shadow|handle [n]\n";
        return 2;
    }
    const std::string workload = argv[1];
    const std::string modeName = argv[2];
    const long long n          = argc > 3 ? std::atoll(argv[3]) : 2000000;
    Mode mode;
    if (modeName == "raw") {
        mode = Mode::Raw;
    } else if (modeName == "raw-poll") {
        mode = Mode::RawPoll;
    } else if (modeName == "shadow") {
        mode = Mode::Shadow;
    } else if (modeName == "handle") {
        mode = Mode::Handle;
    } else {
        std::cerr << "unknown mode\n";
        return 2;
    }

    initialize();
    installShadowStack();
    int ret = 0;
    try {
        long long result = 0;
        if (workload == "keep") {
            std::cout << (mode == Mode::Raw       ? keepRaw(n, false)
                          : mode == Mode::RawPoll ? keepRaw(n, true)
                          : mode == Mode::Shadow  ? keepShadow(n)
                                                  : keepHandle(n))
                      << std::endl;
            mm::autoSpace().unregisterExternalRootTracer(&g_shadow);
            finalize();
            return 0;
        }
        if (workload == "loop") {
            result = mode == Mode::Raw       ? loopRaw(n, false)
                     : mode == Mode::RawPoll ? loopRaw(n, true)
                     : mode == Mode::Shadow  ? loopShadow(n)
                                             : loopHandle(n);
        } else {
            const long long rounds = n / 500;
            for (long long r = 0; r < rounds; ++r) {
                result += mode == Mode::Raw       ? depthRaw(500, false)
                          : mode == Mode::RawPoll ? depthRaw(500, true)
                          : mode == Mode::Shadow  ? depthShadow(500)
                                                  : depthHandle(500);
            }
        }
        std::cout << result << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        ret = 1;
    }
    mm::autoSpace().unregisterExternalRootTracer(&g_shadow);
    finalize();
    return ret;
}
