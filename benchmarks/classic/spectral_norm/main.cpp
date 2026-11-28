// RQ1 classic suite, C++ column: spectral-norm. Same matrix, same iteration count and
// arithmetic order as benchmarks/classic/spectral_norm/main.cml.
#include <cmath>
#include <cstdio>
#include <vector>

static double a(int i, int j) {
    int ij = i + j;
    return 1.0 / (double)(ij * (ij + 1) / 2 + i + 1);
}

static std::vector<double> mul_av(const std::vector<double> &v, int n) {
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        for (int j = 0; j < n; ++j) acc += a(i, j) * v[j];
        out[i] = acc;
    }
    return out;
}

static std::vector<double> mul_atv(const std::vector<double> &v, int n) {
    std::vector<double> out(n);
    for (int j = 0; j < n; ++j) {
        double acc = 0.0;
        for (int i = 0; i < n; ++i) acc += a(i, j) * v[i];
        out[j] = acc;
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
    for (int k = 0; k < 10; ++k) u = mul_atv(mul_av(u, n), n);
    std::vector<double> v = mul_av(u, n);
    double norm = std::sqrt(dot(u, v, n) / dot(v, v, n));
    std::printf("%lld\n", (long long)(norm * 1000000000.0));
    return 0;
}