// RQ1 classic suite, C++ column: spectral-norm. Same matrix, same iteration count, same
// arithmetic order, and the same work count as benchmarks/classic/spectral_norm/main.cml.
#include <cmath>
#include <cstdio>
#include <vector>

struct Run {
    std::vector<double> v;
    long long c;
};

static double a(int i, int j) {
    int ij = i + j;
    return 1.0 / (double)(ij * (ij + 1) / 2 + i + 1);
}

static Run mul_av(const std::vector<double> &v, int n) {
    Run out;
    out.v.resize(n);
    out.c = 0;
    for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int j = 0; j < n; ++j) {
            acc += a(i, j) * v[j];
            ++out.c;
        }
        out.v[i] = acc;
    }
    return out;
}

static Run mul_atv(const std::vector<double> &v, int n) {
    Run out;
    out.v.resize(n);
    out.c = 0;
    for (int j = 0; j < n; ++j) {
        double acc = 0.0;
        for (int i = 0; i < n; ++i) {
            acc += a(i, j) * v[i];
            ++out.c;
        }
        out.v[j] = acc;
    }
    return out;
}

static double dot(const std::vector<double> &u, const std::vector<double> &v, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += u[i] * v[i];
    return acc;
}

int main() {
    const int n = 120;
    std::vector<double> u(n);
    for (int i = 0; i < n; ++i) u[i] = 1.0 / (double)n;
    long long work = 0;
    for (int k = 0; k < 10; ++k) {
        Run av = mul_av(u, n);
        Run ata = mul_atv(av.v, n);
        u = ata.v;
        work += av.c + ata.c;
    }
    Run av = mul_av(u, n);
    work += av.c;
    double norm = std::sqrt(dot(u, av.v, n) / dot(av.v, av.v, n));
    std::printf("%lld\n%lld\n", (long long)(norm * 1000000000.0), work);
    return 0;
}