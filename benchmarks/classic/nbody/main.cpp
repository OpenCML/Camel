// RQ1 classic suite, C++ column: nbody. Same five bodies, same leapfrog step, same energy
// formula and arithmetic order as benchmarks/classic/nbody/main.cml.
#include <cmath>
#include <cstdio>
#include <vector>

static double mass(int i) {
    if (i == 0) return 1.0;
    if (i == 1) return 0.9;
    if (i == 2) return 0.8;
    if (i == 3) return 0.7;
    return 0.6;
}

static double comp(const std::vector<double> &s, int i, int c) { return s[i * 6 + c]; }
static double vel(const std::vector<double> &s, int i, int c) { return s[i * 6 + 3 + c]; }

static double accel(const std::vector<double> &s, int j, int c) {
    double ax = 0.0;
    for (int i = 0; i < 5; ++i) {
        if (i == j) continue;
        double dx = comp(s, i, 0) - comp(s, j, 0);
        double dy = comp(s, i, 1) - comp(s, j, 1);
        double dz = comp(s, i, 2) - comp(s, j, 2);
        double d2 = dx * dx + dy * dy + dz * dz;
        double inv = 1.0 / (d2 * std::sqrt(d2));
        ax += mass(i) * (c == 0 ? dx : (c == 1 ? dy : dz)) * inv;
    }
    return ax;
}

static std::vector<double> step(const std::vector<double> &s, double dt) {
    std::vector<double> nv(15), np(15), out(30);
    for (int k = 0; k < 15; ++k)
        nv[k] = vel(s, k / 3, k % 3) + dt * accel(s, k / 3, k % 3);
    for (int k = 0; k < 15; ++k) np[k] = comp(s, k / 3, k % 3) + dt * nv[k];
    for (int k = 0; k < 30; ++k)
        out[k] = (k % 6 < 3) ? np[k / 6 * 3 + k % 6] : nv[k / 6 * 3 + (k % 6 - 3)];
    return out;
}

static double kinetic(const std::vector<double> &s) {
    double acc = 0.0;
    for (int j = 0; j < 5; ++j)
        acc += 0.5 * mass(j) * (vel(s, j, 0) * vel(s, j, 0) + vel(s, j, 1) * vel(s, j, 1) +
                                vel(s, j, 2) * vel(s, j, 2));
    return acc;
}

static double potential(const std::vector<double> &s) {
    double acc = 0.0;
    for (int i = 0; i < 5; ++i) {
        double p = 0.0;
        for (int j = 0; j < 5; ++j) {
            if (j <= i) continue;
            double dx = comp(s, i, 0) - comp(s, j, 0);
            double dy = comp(s, i, 1) - comp(s, j, 1);
            double dz = comp(s, i, 2) - comp(s, j, 2);
            p += mass(i) * mass(j) / std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        acc += p;
    }
    return acc;
}

int main() {
    const int steps = 5000;
    const double dt = 0.01;
    std::vector<double> st0 = {0.0,        0.0,          0.0,          0.0,          0.0,          0.0,
                               4.841431442, -1.160320044, -0.103622044, 0.606326392,  2.811986844,  -0.025218361,
                               8.343366718, 4.124798564,  -0.403523417, -1.010774346, 1.825662371,  0.008268091,
                               12.894369562, -11.208566928, -0.237546115, 1.131486362, 0.693616560,  0.202992449,
                               15.379697114, -25.919314794, 0.677608747,  1.043144940,  0.498443384, -0.096236481};
    std::vector<double> s = st0;
    for (int k = 0; k < steps; ++k) s = step(s, dt);
    double e = kinetic(s) - potential(s);
    std::printf("%lld\n", (long long)(e * 1000000000.0));
    return 0;
}