import rstd;
import rstd.test;

using namespace rstd::prelude;

int main(int argc, char** argv) {
    rstd::env::args_init(argc, argv);
    return rstd::test::run_registered().to_primitive();
}
