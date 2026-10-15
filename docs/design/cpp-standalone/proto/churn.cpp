// Prototype: a04_string_churn with a reference-counted immutable string (RC=1) or with
// strings that are never freed (RC=0, "no collection"). Two million short-lived strings.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>
namespace rt {
struct StrRep { size_t rc; size_t size; char *data() { return reinterpret_cast<char *>(this + 1); } };
class Str {
  public:
    Str() = default;
    explicit Str(StrRep *r) : rep_(r) {}
    Str(const Str &o) : rep_(o.rep_) { if (rep_) ++rep_->rc; }
    Str(Str &&o) noexcept : rep_(std::exchange(o.rep_, nullptr)) {}
    Str &operator=(Str o) noexcept { std::swap(rep_, o.rep_); return *this; }
    ~Str() {
#if RC
        if (rep_ && --rep_->rc == 0) std::free(rep_);
#endif
    }
    size_t size() const { return rep_ ? rep_->size : 0; }
    const char *data() const { return rep_ ? rep_->data() : ""; }
    static Str make(const char *p, size_t n) {
        auto *r = static_cast<StrRep *>(std::malloc(sizeof(StrRep) + n));
        r->rc = 1; r->size = n; std::memcpy(r->data(), p, n);
        return Str(r);
    }
  private:
    StrRep *rep_ = nullptr;
};
inline Str concat(const Str &a, const Str &b) {
    auto *r = static_cast<StrRep *>(std::malloc(sizeof(StrRep) + a.size() + b.size()));
    r->rc = 1; r->size = a.size() + b.size();
    std::memcpy(r->data(), a.data(), a.size()); std::memcpy(r->data() + a.size(), b.data(), b.size());
    return Str(r);
}
inline Str to_str(int64_t v) { char b[24]; int n = std::snprintf(b, sizeof b, "%lld", (long long)v); return Str::make(b, (size_t)n); }
inline int64_t add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
inline int64_t sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
}
static const rt::Str k_item = rt::Str::make("item-", 5);
static int64_t chunk(int64_t i, int64_t acc) {
    for (;;) {
        if (i == 0) return acc;
        rt::Str s = rt::concat(k_item, rt::to_str(i));
        int64_t next_acc = rt::add(acc, (int64_t)s.size());
        i = rt::sub(i, 1); acc = next_acc;
    }
}
static int64_t rounds(int64_t k, int64_t acc) {
    for (;;) { if (k == 0) return acc; int64_t c = chunk(1000, 0); acc = rt::add(acc, c); k = rt::sub(k, 1); }
}
int main() { std::printf("%lld\n", (long long)rounds(2000, 0)); return 0; }
