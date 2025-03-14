#include <utility>

struct B {};

struct A {
    auto check() & { return 1; }
    auto check() && { return B(); }
};

void test() {
    A    a;
    auto m = a.check();
    auto x = A().check();
}
