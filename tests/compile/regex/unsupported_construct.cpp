import rstd.parse.regex;

constexpr auto INVALID = rstd::parse::regex::compile<"(a)\\1">;

auto main() -> int {
    return 0;
}
