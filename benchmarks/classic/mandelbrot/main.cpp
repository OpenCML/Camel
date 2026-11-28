// RQ1 classic suite, C++ column: mandelbrot. Same grid, same iteration limit, same arithmetic
// order as benchmarks/classic/mandelbrot/main.cml, and the same work count: the number of
// iterations the whole grid cost. Both numbers are printed, so the two columns can be compared.
#include <cstdio>

static int iterate(double zr, double zi, double cr, double ci, int i) {
    if (i == 200) return 200;
    double nr = zr * zr - zi * zi + cr;
    double ni = 2.0 * zr * zi + ci;
    if (nr * nr + ni * ni > 4.0) return i + 1;
    return iterate(nr, ni, cr, ci, i + 1);
}

int main() {
    const int n = 200;
    long long inside = 0;
    long long work = 0;
    for (int k = 0; k < n * n; ++k) {
        double cr = (double)(k / n) * 3.0 / (double)n - 2.0;
        double ci = (double)(k % n) * 2.0 / (double)n - 1.0;
        int used = iterate(0.0, 0.0, cr, ci, 0);
        if (used == 200) ++inside;
        work += used;
    }
    std::printf("%lld\n%lld\n", inside, work);
    return 0;
}