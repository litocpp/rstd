import rstd.parse.regex;

constexpr auto INVALID = rstd::parse::regex::compile<R"(\q)">;

auto main() -> int {
    return 0;
}
