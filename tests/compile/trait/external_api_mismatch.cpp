import rstd;

struct BadExternalClone {};

template<>
struct rstd::Impl<rstd::clone::Clone, BadExternalClone> : rstd::ImplBase<BadExternalClone> {
    auto clone() const -> int { return 0; }
};

int main() {
    BadExternalClone value;
    (void)rstd::as<rstd::clone::Clone>(value).clone();
}
