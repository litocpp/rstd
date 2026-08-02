import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

auto noop(rstd::test::TestContext&) noexcept -> void {
}

} // namespace

int main(int argc, char** argv) {
    rstd::env::args_init(argc, argv);
    auto cases = rstd::array<rstd::test::TestCase, 2> {
        rstd::test::TestCase { "same"_str, &noop },
        rstd::test::TestCase { "same"_str, &noop },
    };
    auto suites = rstd::array<rstd::test::TestSuite, 1> {
        rstd::test::TestSuite { "duplicate"_str, cases.as_slice() },
    };
    return rstd::test::run(suites.as_slice()).to_primitive();
}
