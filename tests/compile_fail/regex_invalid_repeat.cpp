import rstd.parse.regex;

constexpr auto INVALID = rstd::parse::regex::compile<"a{4,2}">;

auto main() -> int {
    return 0;
}
