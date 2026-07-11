import rstd;

struct BadInClassClone : rstd::DefaultInClass<BadInClassClone, rstd::clone::Clone> {
    BadInClassClone()                                          = default;
    BadInClassClone(const BadInClassClone&)                    = delete;
    auto operator=(const BadInClassClone&) -> BadInClassClone& = delete;

    auto clone() const -> int { return 0; }
};

int main() {
    BadInClassClone value;
    (void)rstd::as<rstd::clone::Clone>(value).clone();
}
