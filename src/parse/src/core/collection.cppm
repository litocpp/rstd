export module rstd.parse.core:collection;
export import :consume;

using namespace rstd::prelude;

export namespace rstd::parse
{

template<typename T, rstd::size_t Capacity>
class FixedVec {
    array<Option<T>, Capacity> values_ {};
    usize                      length_ {};

public:
    using value_type = T;

    static constexpr rstd::size_t CAPACITY = Capacity;

    constexpr auto len() const noexcept -> usize { return length_; }
    constexpr auto is_empty() const noexcept -> bool { return length_ == usize(); }
    constexpr auto capacity() const noexcept -> usize { return usize(Capacity); }

    constexpr auto try_push(T value) -> bool {
        if (length_.to_primitive() == Capacity) return false;
        values_[length_] = Some(rstd::move(value));
        ++length_;
        return true;
    }

    constexpr auto operator[](usize index) noexcept [[clang::lifetimebound]] -> T& {
        if (index >= length_) rstd::panic("fixed parse collection index is out of bounds");
        return *values_[index];
    }

    constexpr auto operator[](usize index) const noexcept [[clang::lifetimebound]] -> const T& {
        if (index >= length_) rstd::panic("fixed parse collection index is out of bounds");
        return *values_[index];
    }
};

template<rstd::size_t Capacity>
struct FixedCollectionPolicy {
    template<typename T>
    using collection_type = FixedVec<T, Capacity>;

    template<typename T>
    constexpr auto make() const -> collection_type<T> {
        return {};
    }

    template<typename T>
    constexpr auto push(collection_type<T>& collection, T value) const -> bool {
        return collection.try_push(rstd::move(value));
    }
};

template<typename Policy, typename Value>
using CollectionOf = typename Policy::template collection_type<Value>;

template<typename Policy, typename Value>
concept CollectionPolicyFor =
    requires(Policy policy, CollectionOf<Policy, Value>& collection, Value value) {
        { policy.template make<Value>() } -> mtp::same_as<CollectionOf<Policy, Value>>;
        { policy.push(collection, rstd::move(value)) } -> mtp::same_as<bool>;
    };

} // namespace rstd::parse
