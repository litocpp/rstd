import rstd.parse.regex;

constexpr auto INVALID = rstd::parse::regex::compile<"(abc">;

auto main() -> int {
    return 0;
}
