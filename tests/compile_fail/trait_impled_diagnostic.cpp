import rstd;

struct NoClone {
    NoClone()                                  = default;
    NoClone(const NoClone&)                    = delete;
    auto operator=(const NoClone&) -> NoClone& = delete;
};

int main() {
    NoClone value;
    (void)rstd::as<rstd::clone::Clone>(value).clone();
}
