import rstd;

struct MissingBuilder {};

auto main() -> int {
    (void)rstd::collections::HashMap<rstd::i32, int, MissingBuilder>::with_hasher(
        MissingBuilder {});
}
