import rstd;

struct InnerError {};
struct OuterError {};

template<>
struct rstd::Impl<rstd::convert::From<InnerError>, OuterError> {
    static auto from(InnerError) -> OuterError { return {}; }
};

auto ordinary_err() -> rstd::Result<int, OuterError> {
    return rstd::Err(InnerError {});
}

int main() {
    return ordinary_err().is_ok() ? 0 : 1;
}
