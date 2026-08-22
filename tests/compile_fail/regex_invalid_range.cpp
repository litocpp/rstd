import rstd.parse.regex;

constexpr auto INVALID = rstd::parse::regex::compile<"[z-a]">;

auto main() -> int {
    return 0;
}
