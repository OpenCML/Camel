// RQ1 classic suite, C++ column: fannkuch. Same recursive permutation enumeration and the
// same flip counting as benchmarks/classic/fannkuch/main.cml.
#include <cstdio>
#include <vector>

struct Acc {
    long long c;
    long long m;
};

static std::vector<int> concat(const std::vector<int> &xs, const std::vector<int> &ys) {
    std::vector<int> out(xs);
    out.insert(out.end(), ys.begin(), ys.end());
    return out;
}

static long long flips(std::vector<int> a) {
    if (a[0] == 0) return 0;
    int k = a[0] + 1;
    std::vector<int> next(a.size());
    for (int i = 0; i < k; ++i) next[i] = a[k - 1 - i];
    for (size_t i = k; i < a.size(); ++i) next[i] = a[i];
    return 1 + flips(next);
}

static Acc value(const std::vector<int> &a) {
    long long f = flips(a);
    return Acc{(f % 2 == 0) ? f : -f, f};
}

static Acc combine(Acc p, Acc q) {
    return Acc{p.c + q.c, p.m > q.m ? p.m : q.m};
}

static Acc perms(const std::vector<int> &done, const std::vector<int> &rest) {
    if (rest.empty()) return value(done);
    Acc acc{0, 0};
    for (size_t i = 0; i < rest.size(); ++i) {
        std::vector<int> nextDone = concat(done, {rest[i]});
        std::vector<int> nextRest;
        for (size_t j = 0; j < rest.size(); ++j)
            if (j != i) nextRest.push_back(rest[j]);
        acc = combine(acc, perms(nextDone, nextRest));
    }
    return acc;
}

int main() {
    const int n = 7;
    std::vector<int> all;
    for (int i = 0; i < n; ++i) all.push_back(i);
    Acc r = perms({}, all);
    std::printf("%lld\n%lld\n", r.c, r.m);
    return 0;
}