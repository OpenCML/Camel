// RQ1 classic suite, C++ column: mandelbrot. Same grid, same iteration limit, same arithmetic
// order as benchmarks/classic/mandelbrot/main.cml.
#include <cstdio>

static int iterate(double zr, double zi, double cr, double ci, int i) {
    if (i == 200) return 200;
    double nr = zr * zr - zi * zi + cr;
    double ni = 2.0 * zr * zi + ci;
    if (nr * nr + ni * ni > 4.0) return i;
    return iterate(nr, ni, cr, ci, i + 1);
}

int main() {
    const int n = 200;
    long long inside = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double cr = (double)i * 3.0 / (double)n - 2.0;
            double ci = (double)j * 2.0 / (double)n - 1.0;
            if (iterate(0.0, 0.0, cr, ci, 0) == 200) ++inside;
        }
    }
    std::printf("%lld\n", inside);
    return 0;
}