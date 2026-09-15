export module rstd.core:iter.accumulation;
import :num.types;
export import :iter.traits;

namespace rstd
{

template<typename A, typename B>
    requires(num::Numeric<B> || mtp::is_arithmetic<B>) &&
            mtp::same_as<iter::details::aggregate_item_t<A>, B>
struct Impl<iter::Sum<A>, B> : ImplBase<B> {
    template<iter::has_next I>
    static auto sum(I source) -> B {
        auto step = [](B total, A item) {
            return total + iter::details::observe_item(item);
        };
        return iter::iterator_fold(source, B {}, step);
    }
};

template<typename A, typename B>
    requires(num::Numeric<B> || mtp::is_arithmetic<B>) &&
            mtp::same_as<iter::details::aggregate_item_t<A>, B>
struct Impl<iter::Product<A>, B> : ImplBase<B> {
    template<iter::has_next I>
    static auto product(I source) -> B {
        auto step = [](B total, A item) {
            return total * iter::details::observe_item(item);
        };
        return iter::iterator_fold(source, B { 1 }, step);
    }
};

} // namespace rstd
