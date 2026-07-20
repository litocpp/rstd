import rstd;

struct InvalidState {};

struct InvalidBuilder {
    using Hasher = InvalidState;

    auto build_hasher() const noexcept -> Hasher { return {}; }
};

auto main() -> int {
    (void)rstd::collections::HashMap<rstd::i32, int, InvalidBuilder>::with_hasher(
        InvalidBuilder {});
}
