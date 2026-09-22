import rstd.parse.regex;

constexpr auto INVALID = rstd::parse::regex::compile<"(?<x>a)(?<x>b)">;

auto main() -> int {
    return 0;
}
