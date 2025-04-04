module;
#include <string>
#include <format>
export module rstd.string;

export import rstd.core;

namespace rstd::string
{

export using String = std::string;
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

export template<typename... _Args>
using format_string = std::basic_format_string<char, std::type_identity_t<_Args>...>;

export template<typename... Args>
[[nodiscard]]
inline String format(format_string<Args...> fmt, Args&&... args) {
    return std::vformat(fmt.get(), std::make_format_args(args...));
}

template<meta::same_as<string::ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> string::String { return format("{}", this->self()); }
};

} // namespace rstd
