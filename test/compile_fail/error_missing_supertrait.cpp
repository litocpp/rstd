import rstd;

struct MissingDebug {};

namespace rstd
{

template<>
struct Impl<fmt::Display, MissingDebug> : ImplBase<MissingDebug> {
    auto fmt(fmt::Formatter&) const -> bool { return true; }
};

template<>
struct Impl<error::Error, MissingDebug> : DefaultInImpl<error::Error, MissingDebug> {};

} // namespace rstd

int main() {
    MissingDebug value;
    static_cast<void>(rstd::as<rstd::error::Error>(value));
}
