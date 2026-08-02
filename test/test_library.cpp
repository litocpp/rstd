import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

auto passes(rstd::test::TestContext& context) noexcept -> void {
    context.expect(true, "true must pass"_str);
}

auto expectation_fails(rstd::test::TestContext& context) noexcept -> void {
    context.expect(false, "expected failure"_str);
}

auto returns_early(rstd::test::TestContext& context) noexcept -> void {
    if (! context.expect(false, "required value is missing"_str)) return;
    context.fail("unreachable failure"_str);
}

} // namespace

int main(int argc, char** argv) {
    rstd::env::args_init(argc, argv);
    auto cases = rstd::array<rstd::test::TestCase, 3> {
        rstd::test::TestCase { "passes"_str, &passes },
        rstd::test::TestCase { "expectation-fails"_str, &expectation_fails },
        rstd::test::TestCase { "returns-early"_str, &returns_early },
    };
    auto suites = rstd::array<rstd::test::TestSuite, 1> {
        rstd::test::TestSuite { "library"_str, cases.as_slice() },
    };
    return rstd::test::run(suites.as_slice()).to_primitive();
}
