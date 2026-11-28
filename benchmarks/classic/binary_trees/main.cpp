// RQ1 classic suite, C++ column: binary-trees. Same shape as
// benchmarks/classic/binary_trees/main.cml: a node is its list of children.
#include <cstdio>
#include <vector>

struct Node {
    std::vector<Node> children;
};

static Node make(int d) {
    Node n;
    if (d != 0) {
        n.children.push_back(make(d - 1));
        n.children.push_back(make(d - 1));
    }
    return n;
}

static long long check(const Node &n) {
    long long total = 1;
    for (const Node &c : n.children) total += check(c);
    return total;
}

int main() {
    const int depth = 16;
    long long total = 0;
    for (int q = 0; q < 3; ++q) total += check(make(depth));
    std::printf("%lld\n", total);
    return 0;
}