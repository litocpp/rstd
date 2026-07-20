import rstd;

struct CallableBuilder {
    template<typename T>
    auto operator()(const T&) const noexcept -> rstd::u64 {
        return rstd::u64();
    }
};

auto main() -> int {
    (void)rstd::collections::HashMap<rstd::i32, int, CallableBuilder>::with_hasher(
        CallableBuilder {});
}
