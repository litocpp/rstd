#include <rstd/test/gtest.hpp>
import rstd.test;

int main() {
    return rstd::test::run_registered().to_primitive();
}
