import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

auto option_basic(rstd::test::TestContext& context) noexcept -> void {
    auto value = Some(i32(42));
    context.expect(value.is_some(), "Some must contain a value"_str);
    context.expect(*value == i32(42), "Some must preserve its value"_str);
}

auto result_basic(rstd::test::TestContext& context) noexcept -> void {
    auto value = Result<i32, i32>(Ok(i32(7)));
    if (! context.expect(value.is_ok(), "Ok must report success"_str)) return;
    context.expect(*value == i32(7), "Ok must preserve its value"_str);
}

auto string_ascii(rstd::test::TestContext& context) noexcept -> void {
    auto value = String::make("tenon"_str);
    value.push_ascii('!');
    context.expect(value == "tenon!"_str, "push_ascii(char) must append ASCII"_str);
}

} // namespace

int main(int argc, char** argv) {
    rstd::env::args_init(argc, argv);
    auto cases = rstd::array<rstd::test::TestCase, 3> {
        rstd::test::TestCase { "option-basic"_str, &option_basic },
        rstd::test::TestCase { "result-basic"_str, &result_basic },
        rstd::test::TestCase { "string-ascii"_str, &string_ascii },
    };
    auto suites = rstd::array<rstd::test::TestSuite, 1> {
        rstd::test::TestSuite { "rstd"_str, cases.as_slice() },
    };
    return rstd::test::run(suites.as_slice()).to_primitive();
}
