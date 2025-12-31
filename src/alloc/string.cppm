export module rstd.string;
export import rstd.core;

namespace rstd::string
{

export using String = cppstd::string;
export struct ToString {
    template<typename T, typename = void>
    struct Api {
        auto to_string() const -> String;
    };

    template<typename T>
    using TCollect = TraitCollect<&T::to_string>;
};

} // namespace rstd::string
namespace rstd
{

export using String = string::String;
export template<meta::same_as<string::ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> string::String { return format("{}", this->self()); }
};

export template<Impled<string::ToString> A>
auto to_string(A&& a) {
    // use lvalue
    return as<string::ToString>(a).to_string();
}

} // namespace rstd
