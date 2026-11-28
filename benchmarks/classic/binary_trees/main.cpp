// RQ1 classic suite, C++ column: binary-trees. Same shape as
// benchmarks/classic/binary_trees/main.cml: a node is its list of children. It prints the same
// two numbers: the node count and the traversal's work count (node visits plus the child slots
// walked), so the work count is larger than the node count.
#include <cstdio>
#include <vector>

struct Node {
    std::vector<Node> children;
};

struct Count {
    long long n;
    long long w;
};

static Node make(int d) {
    Node n;
    if (d != 0) {
        n.children.push_back(make(d - 1));
        n.children.push_back(make(d - 1));
    }
    return n;
}

static Count check(const Node &n) {
    Count kids{0, 0};
    for (const Node &c : n.children) {
        Count one = check(c);
        kids.n += one.n;
        kids.w += one.w;
    }
    return Count{kids.n + 1, kids.w + 1 + (long long)n.children.size()};
}

int main() {
    const int depth = 16;
    Count t{0, 0};
    for (int q = 0; q < 3; ++q) {
        Count one = check(make(depth));
        t.n += one.n;
        t.w += one.w;
    }
    std::printf("%lld\n%lld\n", t.n, t.w);
    return 0;
}