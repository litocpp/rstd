export module rstd.core:choice;

namespace rstd
{

enum class XX
{
    x1,
    x2
};

struct XXData {
    template<auto>
    struct d;

    template<>
    struct d<XX::x1> {};
};
consteval auto data(XX x) -> XXData;

} // namespace rstd