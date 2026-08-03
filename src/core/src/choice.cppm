export module rstd.core:choice;
import :num.types;
import :intrinsics;
export import :core;

namespace rstd::choice_detail
{

template<rstd::size_t N>
struct smallest_index {
    using type = mtp::
        cond<(N <= 0xff), rstd::uint8_t, mtp::cond<(N <= 0xffff), rstd::uint16_t, rstd::uint32_t>>;
};

template<rstd::size_t N>
using smallest_index_t = typename smallest_index<N>::type;

template<rstd::size_t I>
struct in_place_index_t {
    explicit constexpr in_place_index_t() = default;
};

template<rstd::size_t I>
inline constexpr in_place_index_t<I> in_place_index {};

template<rstd::size_t I, typename T, typename... Rest>
struct type_at_impl {
    using type = typename type_at_impl<I - 1, Rest...>::type;
};

template<typename T, typename... Rest>
struct type_at_impl<0, T, Rest...> {
    using type = T;
};

template<rstd::size_t I, typename... Ts>
using type_at_t = typename type_at_impl<I, Ts...>::type;

template<rstd::size_t Base, typename... Ts>
union union_pack;

template<rstd::size_t Base>
union union_pack<Base> {
    constexpr union_pack() noexcept {}
    constexpr union_pack(union_pack const&) noexcept                    = default;
    constexpr union_pack(union_pack&&) noexcept                         = default;
    constexpr auto operator=(union_pack const&) noexcept -> union_pack& = default;
    constexpr auto operator=(union_pack&&) noexcept -> union_pack&      = default;
    constexpr ~union_pack() noexcept                                    = default;
};

template<rstd::size_t Base, typename T, typename... Rest>
union union_pack<Base, T, Rest...> {
    T                             head;
    union_pack<Base + 1, Rest...> tail;

    constexpr union_pack() noexcept {}

    template<rstd::size_t I, typename... Args>
        requires(I == Base) && mtp::init<T, Args...>
    explicit constexpr union_pack(in_place_index_t<I>,
                                  Args&&... args) noexcept(mtp::noex_init<T, Args...>)
        : head(rstd::forward<Args>(args)...) {}

    template<rstd::size_t I, typename... Args>
        requires(I > Base) && mtp::init<type_at_t<I - Base, T, Rest...>, Args...>
    explicit constexpr union_pack(in_place_index_t<I>, Args&&... args) noexcept(
        mtp::noex_init<type_at_t<I - Base, T, Rest...>, Args...>)
        : tail(in_place_index<I>, rstd::forward<Args>(args)...) {}

    constexpr union_pack(union_pack const&)
        requires(mtp::triv_copy<T> && (mtp::triv_copy<Rest> && ...))
    = default;
    constexpr union_pack(union_pack&&)
        requires(mtp::triv_move<T> && (mtp::triv_move<Rest> && ...))
    = default;
    constexpr auto operator=(union_pack const&) -> union_pack&
        requires(mtp::triv_assign_copy<T> && (mtp::triv_assign_copy<Rest> && ...))
    = default;
    constexpr auto operator=(union_pack&&) -> union_pack&
        requires(mtp::triv_assign_move<T> && (mtp::triv_assign_move<Rest> && ...))
    = default;

    constexpr ~union_pack() noexcept
        requires(mtp::triv_drop<T> && (mtp::triv_drop<Rest> && ...))
    = default;

    constexpr ~union_pack() noexcept
        requires(! (mtp::triv_drop<T> && (mtp::triv_drop<Rest> && ...)))
    {}

    template<rstd::size_t I, typename... Args>
    constexpr void
    construct(Args&&... args) noexcept(mtp::noex_init<type_at_t<I - Base, T, Rest...>, Args...>) {
        if constexpr (I == Base) {
            rstd::construct_at(rstd::addressof(head), rstd::forward<Args>(args)...);
        } else {
            rstd::construct_at(rstd::addressof(tail));
            tail.template construct<I>(rstd::forward<Args>(args)...);
        }
    }

    template<rstd::size_t I>
    constexpr void destroy() noexcept {
        if constexpr (I == Base) {
            rstd::destroy_at(rstd::addressof(head));
        } else {
            tail.template destroy<I>();
            rstd::destroy_at(rstd::addressof(tail));
        }
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get() & noexcept {
        if constexpr (I == Base)
            return (head);
        else
            return tail.template get<I>();
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get() const& noexcept {
        if constexpr (I == Base)
            return (head);
        else
            return tail.template get<I>();
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get() && noexcept {
        if constexpr (I == Base)
            return static_cast<T&&>(head);
        else
            return static_cast<union_pack<Base + 1, Rest...>&&>(tail).template get<I>();
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get() const&& noexcept {
        if constexpr (I == Base)
            return static_cast<T const&&>(head);
        else
            return static_cast<union_pack<Base + 1, Rest...> const&&>(tail).template get<I>();
    }
};

template<typename... Ts>
class storage {
    using index_type = smallest_index_t<sizeof...(Ts)>;

    static constexpr rstd::size_t count         = sizeof...(Ts);
    static constexpr index_type   invalid_index = static_cast<index_type>(count);

    union_pack<0, Ts...> data_;
    index_type           index_ = invalid_index;

    template<rstd::size_t I>
    constexpr void destroy_impl() noexcept {
        if constexpr (I < count) {
            if (index_ == static_cast<index_type>(I))
                data_.template destroy<I>();
            else
                destroy_impl<I + 1>();
        }
    }

    template<rstd::size_t I>
    constexpr void copy_impl(storage const& other) {
        if constexpr (I < count) {
            if (other.index_ == static_cast<index_type>(I)) {
                construct(in_place_index<I>, other.data_.template get<I>());
            } else {
                copy_impl<I + 1>(other);
            }
        } else {
            rstd::intrinsics::abort();
        }
    }

    template<rstd::size_t I>
    constexpr void move_impl(storage&& other) noexcept((mtp::noex_move<Ts> && ...)) {
        if constexpr (I < count) {
            if (other.index_ == static_cast<index_type>(I)) {
                construct(in_place_index<I>, rstd::move(other.data_).template get<I>());
            } else {
                move_impl<I + 1>(rstd::move(other));
            }
        } else {
            rstd::intrinsics::abort();
        }
    }

    template<rstd::size_t I>
    constexpr void copy_assign_impl(storage const& other) noexcept((mtp::noex_assign_copy<Ts> &&
                                                                    ...)) {
        if constexpr (I < count) {
            if (index_ == static_cast<index_type>(I)) {
                data_.template get<I>() = other.data_.template get<I>();
            } else {
                copy_assign_impl<I + 1>(other);
            }
        } else {
            rstd::intrinsics::abort();
        }
    }

    template<rstd::size_t I>
    constexpr void move_assign_impl(storage&& other) noexcept((mtp::noex_assign_move<Ts> && ...)) {
        if constexpr (I < count) {
            if (index_ == static_cast<index_type>(I)) {
                data_.template get<I>() = rstd::move(other.data_).template get<I>();
            } else {
                move_assign_impl<I + 1>(rstd::move(other));
            }
        } else {
            rstd::intrinsics::abort();
        }
    }

    template<rstd::size_t I, typename... Args>
        requires(I < count) && mtp::init<type_at_t<I, Ts...>, Args...>
    constexpr void
    construct(in_place_index_t<I>,
              Args&&... args) noexcept(mtp::noex_init<type_at_t<I, Ts...>, Args...>) {
        data_.template construct<I>(rstd::forward<Args>(args)...);
        index_ = static_cast<index_type>(I);
    }

public:
    constexpr storage() noexcept: data_(), index_(invalid_index) {}

    template<rstd::size_t I, typename... Args>
        requires(I < count) && mtp::init<type_at_t<I, Ts...>, Args...>
    explicit constexpr storage(in_place_index_t<I>, Args&&... args) noexcept(
        mtp::noex_init<type_at_t<I, Ts...>, Args...>)
        : data_(in_place_index<I>, rstd::forward<Args>(args)...),
          index_(static_cast<index_type>(I)) {}

    constexpr storage(storage const&)
        requires(mtp::triv_copy<Ts> && ...)
    = default;

    constexpr storage(storage const& other)
        requires((mtp::copy<Ts> && ...) && ! (mtp::triv_copy<Ts> && ...))
        : data_(), index_(invalid_index) {
        copy_impl<0>(other);
    }

    constexpr storage(storage const&)
        requires(! (mtp::copy<Ts> && ...))
    = delete;

    constexpr storage(storage&&)
        requires(mtp::triv_move<Ts> && ...)
    = default;

    constexpr storage(storage&& other) noexcept((mtp::noex_move<Ts> && ...))
        requires((mtp::move<Ts> && ...) && ! (mtp::triv_move<Ts> && ...))
        : data_(), index_(invalid_index) {
        move_impl<0>(rstd::move(other));
    }

    constexpr storage(storage&&)
        requires(! (mtp::move<Ts> && ...))
    = delete;

    constexpr ~storage()
        requires(mtp::triv_drop<Ts> && ...)
    = default;

    constexpr ~storage()
        requires(! (mtp::triv_drop<Ts> && ...))
    {
        destroy();
    }

    constexpr auto operator=(storage const&) -> storage&
        requires((mtp::copy<Ts> && ...) && (mtp::assign_copy<Ts> && ...) &&
                 (mtp::triv_assign_copy<Ts> && ...))
    = default;

    constexpr auto operator=(storage const& rhs) noexcept((mtp::noex_copy<Ts> && ...) &&
                                                          (mtp::noex_assign_copy<Ts> && ...))
        -> storage&
        requires((mtp::copy<Ts> && ...) && (mtp::assign_copy<Ts> && ...) &&
                 ! (mtp::triv_assign_copy<Ts> && ...))
    {
        if (this == rstd::addressof(rhs)) return *this;
        if (index_ == rhs.index_) {
            copy_assign_impl<0>(rhs);
        } else if constexpr ((mtp::noex_move<Ts> && ...)) {
            storage tmp(rhs);
            destroy();
            move_impl<0>(rstd::move(tmp));
        } else {
            destroy();
            copy_impl<0>(rhs);
        }
        return *this;
    }

    constexpr auto operator=(storage const&) -> storage&
        requires(! ((mtp::copy<Ts> && ...) && (mtp::assign_copy<Ts> && ...)))
    = delete;

    constexpr auto operator=(storage&&) -> storage&
        requires((mtp::move<Ts> && ...) && (mtp::assign_move<Ts> && ...) &&
                 (mtp::triv_assign_move<Ts> && ...))
    = default;

    constexpr auto operator=(storage&& rhs) noexcept((mtp::noex_move<Ts> && ...) &&
                                                     (mtp::noex_assign_move<Ts> && ...)) -> storage&
        requires((mtp::move<Ts> && ...) && (mtp::assign_move<Ts> && ...) &&
                 ! (mtp::triv_assign_move<Ts> && ...))
    {
        if (this == rstd::addressof(rhs)) return *this;
        if (index_ == rhs.index_) {
            move_assign_impl<0>(rstd::move(rhs));
        } else {
            destroy();
            move_impl<0>(rstd::move(rhs));
        }
        return *this;
    }

    constexpr auto operator=(storage&&) -> storage&
        requires(! ((mtp::move<Ts> && ...) && (mtp::assign_move<Ts> && ...)))
    = delete;

    template<rstd::size_t I, typename... Args>
        requires(I < count) && mtp::init<type_at_t<I, Ts...>, Args...>
    constexpr void replace(in_place_index_t<I>,
                           Args&&... args) noexcept(mtp::noex_init<type_at_t<I, Ts...>, Args...>) {
        using next_type = type_at_t<I, Ts...>;
        if constexpr (mtp::noex_init<next_type, Args...>) {
            destroy();
            construct(in_place_index<I>, rstd::forward<Args>(args)...);
        } else if constexpr (mtp::noex_move<next_type>) {
            next_type tmp(rstd::forward<Args>(args)...);
            destroy();
            construct(in_place_index<I>, rstd::move(tmp));
        } else {
            destroy();
            construct(in_place_index<I>, rstd::forward<Args>(args)...);
        }
    }

    constexpr void destroy() noexcept {
        if (index_ != invalid_index) {
            destroy_impl<0>();
            index_ = invalid_index;
        }
    }

    [[nodiscard]]
    constexpr auto index() const noexcept -> rstd::size_t {
        return static_cast<rstd::size_t>(index_);
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr auto is(in_place_index_t<I>) const noexcept -> bool {
        return index_ == static_cast<index_type>(I);
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) & noexcept {
        return data_.template get<I>();
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) const& noexcept {
        return data_.template get<I>();
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) && noexcept {
        return rstd::move(data_).template get<I>();
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) const&& noexcept {
        return static_cast<union_pack<0, Ts...> const&&>(data_).template get<I>();
    }
};

template<rstd::size_t Count>
class tag_storage {
    using index_type = smallest_index_t<Count>;
    index_type index_ {};

public:
    template<rstd::size_t I>
        requires(I < Count)
    explicit constexpr tag_storage(in_place_index_t<I>) noexcept
        : index_(static_cast<index_type>(I)) {}

    template<rstd::size_t I>
        requires(I < Count)
    constexpr void replace(in_place_index_t<I>) noexcept {
        index_ = static_cast<index_type>(I);
    }

    [[nodiscard]]
    constexpr auto index() const noexcept -> rstd::size_t {
        return static_cast<rstd::size_t>(index_);
    }

    template<rstd::size_t I>
    [[nodiscard]]
    constexpr auto is(in_place_index_t<I>) const noexcept -> bool {
        return index_ == static_cast<index_type>(I);
    }
};

struct no_payload {};

template<typename... Ts>
struct payload_type;

template<>
struct payload_type<void> {
    using type = void;
};

template<typename T>
struct payload_type<T> {
    using type = T;
};

template<typename T, typename U, typename... Rest>
struct payload_type<T, U, Rest...> {
    using type = rstd::tuple<T, U, Rest...>;
};

template<typename T>
struct stored_type {
    using type = T;
};

template<>
struct stored_type<void> {
    using type = no_payload;
};

template<typename Case>
using case_payload_t = typename Case::payload_type;

template<typename Case>
using case_stored_t = typename stored_type<case_payload_t<Case>>::type;

template<bool AllVoid, typename... Cases>
struct storage_for_impl;

template<typename... Cases>
struct storage_for_impl<false, Cases...> {
    using type = storage<case_stored_t<Cases>...>;
};

template<typename... Cases>
struct storage_for_impl<true, Cases...> {
    using type = tag_storage<sizeof...(Cases)>;
};

template<typename... Cases>
using storage_for_t =
    typename storage_for_impl<(mtp::same_as<case_payload_t<Cases>, void> && ...), Cases...>::type;

template<rstd::size_t I, typename Case, typename... Rest>
struct case_at_impl : case_at_impl<I - 1, Rest...> {};

template<typename Case, typename... Rest>
struct case_at_impl<0, Case, Rest...> {
    using type = Case;
};

template<rstd::size_t I, typename... Cases>
using case_at_t = typename case_at_impl<I, Cases...>::type;

template<auto V, typename First, typename... Rest>
consteval auto case_index() -> rstd::size_t {
    static_assert((First::value == V) || ((Rest::value == V) || ...),
                  "the requested tag is not present in rstd::Choice");
    constexpr typename First::tag_type tags[] = { First::value, Rest::value... };
    for (rstd::size_t i = 0; i < 1 + sizeof...(Rest); ++i) {
        if (tags[i] == V) return i;
    }
    return 1 + sizeof...(Rest);
}

template<auto V, typename First, typename... Rest>
using case_for_t = case_at_t<case_index<V, First, Rest...>(), First, Rest...>;

template<typename...>
inline constexpr bool dependent_false = false;

} // namespace rstd::choice_detail

namespace rstd::enum_detail
{

export [[noreturn]]
inline void bad_enum_state() noexcept {
    rstd::intrinsics::abort();
}

export template<typename Like, typename T>
[[nodiscard]]
constexpr decltype(auto) forward_like(T&& x) noexcept {
    using U = mtp::rm_ref<T>;
    if constexpr (mtp::is_ref_lv<Like>) {
        if constexpr (mtp::is_const<mtp::rm_ref<Like>>)
            return static_cast<U const&>(x);
        else
            return static_cast<U&>(x);
    } else {
        if constexpr (mtp::is_const<mtp::rm_ref<Like>>)
            return static_cast<U const&&>(x);
        else
            return static_cast<U&&>(x);
    }
}

} // namespace rstd::enum_detail

namespace rstd
{

export template<auto Value>
struct choice_tag {
    static constexpr auto value = Value;
};

export template<auto TagValue, typename... PayloadTypes>
struct choice_case {
    static_assert(sizeof...(PayloadTypes) > 0,
                  "each rstd::Choice case requires a payload type or void");
    static_assert(sizeof...(PayloadTypes) == 1 || (! mtp::same_as<PayloadTypes, void> && ...),
                  "void must be the only payload type in an rstd::Choice case");
    static_assert((! mtp::is_ref<PayloadTypes> && ...),
                  "reference payloads are not supported by rstd::Choice");

    static constexpr auto value = TagValue;
    using tag_type              = decltype(TagValue);
    using payload_type          = typename choice_detail::payload_type<PayloadTypes...>::type;
};

export template<typename... Cases>
class Choice {
    static_assert(choice_detail::dependent_false<Cases...>,
                  "rstd::Choice requires at least one choice_case");
};

export template<typename First, typename... Rest>
class Choice<First, Rest...> final {
    using first_case   = First;
    using storage_type = choice_detail::storage_for_t<First, Rest...>;

public:
    using Tag = typename first_case::tag_type;

private:
    static constexpr rstd::size_t case_count = 1 + sizeof...(Rest);
    static constexpr bool same_tag_types     = (mtp::same_as<Tag, typename Rest::tag_type> && ...);
    static constexpr bool all_copy = mtp::copy<choice_detail::case_stored_t<First>> &&
                                     (mtp::copy<choice_detail::case_stored_t<Rest>> && ...);
    static constexpr bool all_move = mtp::move<choice_detail::case_stored_t<First>> &&
                                     (mtp::move<choice_detail::case_stored_t<Rest>> && ...);
    static constexpr bool all_copy_assign =
        all_copy && mtp::assign_copy<choice_detail::case_stored_t<First>> &&
        (mtp::assign_copy<choice_detail::case_stored_t<Rest>> && ...);
    static constexpr bool all_move_assign =
        all_move && mtp::assign_move<choice_detail::case_stored_t<First>> &&
        (mtp::assign_move<choice_detail::case_stored_t<Rest>> && ...);

    static_assert(same_tag_types, "all rstd::Choice tags must have the same type");

    static consteval auto tags_are_unique() -> bool {
        if constexpr (! same_tag_types) {
            return false;
        } else {
            constexpr Tag tags[] = { First::value, Rest::value... };
            for (rstd::size_t i = 0; i < case_count; ++i) {
                for (rstd::size_t j = i + 1; j < case_count; ++j) {
                    if (tags[i] == tags[j]) return false;
                }
            }
            return true;
        }
    }

    static_assert(tags_are_unique(), "all rstd::Choice tags must be unique");

    template<Tag V>
    static consteval auto index_for() -> rstd::size_t {
        return choice_detail::case_index<V, First, Rest...>();
    }

    template<Tag V>
    using case_for = choice_detail::case_for_t<V, First, Rest...>;

    template<Tag V>
    using stored_for = choice_detail::case_stored_t<case_for<V>>;

    template<rstd::size_t I>
    using stored_at = choice_detail::case_stored_t<choice_detail::case_at_t<I, First, Rest...>>;

    template<rstd::size_t I>
    using case_at = choice_detail::case_at_t<I, First, Rest...>;

    template<rstd::size_t I, typename Self, typename Visitor>
    static consteval auto case_invocable() -> bool {
        constexpr auto value = case_at<I>::value;
        if constexpr (mtp::same_as<typename case_at<I>::payload_type, void>) {
            return requires(Visitor&& visitor) {
                rstd::forward<Visitor>(visitor)(choice_tag<value> {});
            };
        } else {
            return requires(Self&& self, Visitor&& visitor) {
                rstd::forward<Visitor>(visitor)(
                    choice_tag<value> {},
                    rstd::forward<Self>(self).storage_.get(choice_detail::in_place_index<I>));
            };
        }
    }

    template<rstd::size_t I, typename Self, typename Visitor>
    static consteval auto all_cases_invocable() -> bool {
        if constexpr (! case_invocable<I, Self, Visitor>()) {
            return false;
        } else if constexpr (I + 1 < case_count) {
            return all_cases_invocable<I + 1, Self, Visitor>();
        } else {
            return true;
        }
    }

    template<rstd::size_t I, typename Self, typename Visitor>
    static constexpr decltype(auto)
    invoke_case(Self&& self, Visitor&& visitor) noexcept(case_noexcept<I, Self, Visitor>()) {
        constexpr auto value = case_at<I>::value;
        if constexpr (mtp::same_as<typename case_at<I>::payload_type, void>) {
            return rstd::forward<Visitor>(visitor)(choice_tag<value> {});
        } else {
            return rstd::forward<Visitor>(visitor)(
                choice_tag<value> {},
                rstd::forward<Self>(self).storage_.get(choice_detail::in_place_index<I>));
        }
    }

    template<rstd::size_t I, typename Self, typename Visitor>
    static consteval auto case_noexcept() -> bool {
        constexpr auto value = case_at<I>::value;
        if constexpr (mtp::same_as<typename case_at<I>::payload_type, void>) {
            return noexcept(rstd::forward<Visitor>(mtp::declval<Visitor>())(choice_tag<value> {}));
        } else {
            return noexcept(rstd::forward<Visitor>(mtp::declval<Visitor>())(
                choice_tag<value> {},
                rstd::forward<Self>(mtp::declval<Self>())
                    .storage_.get(choice_detail::in_place_index<I>)));
        }
    }

    template<rstd::size_t I, typename Result, typename Self, typename Visitor>
    static consteval auto all_case_results_match() -> bool {
        if constexpr (! mtp::same_as<decltype(invoke_case<I>(mtp::declval<Self>(),
                                                             mtp::declval<Visitor>())),
                                     Result>) {
            return false;
        } else if constexpr (I + 1 < case_count) {
            return all_case_results_match<I + 1, Result, Self, Visitor>();
        } else {
            return true;
        }
    }

    template<typename Self, typename Visitor>
    static consteval auto valid_visitor() -> bool {
        if constexpr (! all_cases_invocable<0, Self, Visitor>()) {
            return false;
        } else {
            using Result = decltype(invoke_case<0>(mtp::declval<Self>(), mtp::declval<Visitor>()));
            return all_case_results_match<0, Result, Self, Visitor>();
        }
    }

    template<rstd::size_t I, typename Self, typename Visitor>
    static consteval auto all_cases_noexcept() -> bool {
        if constexpr (! case_noexcept<I, Self, Visitor>()) {
            return false;
        } else if constexpr (I + 1 < case_count) {
            return all_cases_noexcept<I + 1, Self, Visitor>();
        } else {
            return true;
        }
    }

    template<rstd::size_t I, typename Self, typename Visitor>
    static constexpr decltype(auto)
    visit_active(Self&& self, Visitor&& visitor) noexcept(all_cases_noexcept<0, Self, Visitor>()) {
        if (self.storage_.index() == I) {
            return invoke_case<I>(rstd::forward<Self>(self), rstd::forward<Visitor>(visitor));
        }
        if constexpr (I + 1 < case_count) {
            return visit_active<I + 1>(rstd::forward<Self>(self), rstd::forward<Visitor>(visitor));
        } else {
            enum_detail::bad_enum_state();
        }
    }

    template<rstd::size_t I, typename... Args>
        requires mtp::init<stored_at<I>, Args...>
    explicit constexpr Choice(choice_detail::in_place_index_t<I>,
                              Args&&... args) noexcept(mtp::noex_init<stored_at<I>, Args...>)
        : storage_(choice_detail::in_place_index<I>, rstd::forward<Args>(args)...) {}

    storage_type storage_;

public:
    Choice() = delete;

    constexpr Choice(Choice const&)
        requires all_copy
    = default;
    constexpr Choice(Choice const&)
        requires(! all_copy)
    = delete;

    constexpr Choice(Choice&&)
        requires all_move
    = default;
    constexpr Choice(Choice&&)
        requires(! all_move)
    = delete;

    constexpr auto operator=(Choice const&) -> Choice&
        requires all_copy_assign
    = default;
    constexpr auto operator=(Choice const&) -> Choice&
        requires(! all_copy_assign)
    = delete;

    constexpr auto operator=(Choice&&) -> Choice&
        requires all_move_assign
    = default;
    constexpr auto operator=(Choice&&) -> Choice&
        requires(! all_move_assign)
    = delete;

    constexpr ~Choice() = default;

    template<Tag V>
    using TypeForTag = typename case_for<V>::payload_type;

    template<Tag V, typename... Args>
        requires mtp::init<stored_for<V>, Args...>
    [[nodiscard]]
    static constexpr auto with(Args&&... args) noexcept(mtp::noex_init<stored_for<V>, Args...>)
        -> Choice {
        return Choice(choice_detail::in_place_index<index_for<V>()>, rstd::forward<Args>(args)...);
    }

    [[nodiscard]]
    constexpr auto which() const noexcept -> Tag {
        constexpr Tag tags[] = { First::value, Rest::value... };
        return tags[index()];
    }

    [[nodiscard]]
    constexpr auto index() const noexcept -> rstd::size_t {
        auto const value = storage_.index();
        if (value >= case_count) enum_detail::bad_enum_state();
        return value;
    }

    template<Tag V>
    [[nodiscard]]
    constexpr auto is() const noexcept -> bool {
        return storage_.is(choice_detail::in_place_index<index_for<V>()>);
    }

    template<Tag V>
        requires(! mtp::same_as<TypeForTag<V>, void>)
    [[nodiscard]]
    constexpr decltype(auto) as() & noexcept {
        if (! is<V>()) enum_detail::bad_enum_state();
        return storage_.get(choice_detail::in_place_index<index_for<V>()>);
    }

    template<Tag V>
        requires(! mtp::same_as<TypeForTag<V>, void>)
    [[nodiscard]]
    constexpr decltype(auto) as() const& noexcept {
        if (! is<V>()) enum_detail::bad_enum_state();
        return storage_.get(choice_detail::in_place_index<index_for<V>()>);
    }

    template<Tag V>
        requires(! mtp::same_as<TypeForTag<V>, void>)
    [[nodiscard]]
    constexpr decltype(auto) as() && noexcept {
        if (! is<V>()) enum_detail::bad_enum_state();
        return rstd::move(storage_).get(choice_detail::in_place_index<index_for<V>()>);
    }

    template<Tag V>
        requires(! mtp::same_as<TypeForTag<V>, void>)
    [[nodiscard]]
    constexpr decltype(auto) as() const&& noexcept {
        if (! is<V>()) enum_detail::bad_enum_state();
        return static_cast<storage_type const&&>(storage_).get(
            choice_detail::in_place_index<index_for<V>()>);
    }

    template<Tag V, typename... Args>
        requires requires(storage_type& storage, Args&&... args) {
            storage.replace(choice_detail::in_place_index<index_for<V>()>,
                            rstd::forward<Args>(args)...);
        }
    constexpr void set(Args&&... args) noexcept(
        noexcept(storage_.replace(choice_detail::in_place_index<index_for<V>()>,
                                  rstd::forward<Args>(args)...))) {
        storage_.replace(choice_detail::in_place_index<index_for<V>()>,
                         rstd::forward<Args>(args)...);
    }

    template<typename Visitor>
        requires(valid_visitor<Choice&, Visitor>())
    constexpr decltype(auto)
    visit(Visitor&& visitor) & noexcept(all_cases_noexcept<0, Choice&, Visitor>()) {
        return visit_active<0>(*this, rstd::forward<Visitor>(visitor));
    }

    template<typename Visitor>
        requires(valid_visitor<const Choice&, Visitor>())
    constexpr decltype(auto)
    visit(Visitor&& visitor) const& noexcept(all_cases_noexcept<0, const Choice&, Visitor>()) {
        return visit_active<0>(*this, rstd::forward<Visitor>(visitor));
    }

    template<typename Visitor>
        requires(valid_visitor<Choice &&, Visitor>())
    constexpr decltype(auto)
    visit(Visitor&& visitor) && noexcept(all_cases_noexcept<0, Choice&&, Visitor>()) {
        return visit_active<0>(rstd::move(*this), rstd::forward<Visitor>(visitor));
    }

    template<typename Visitor>
        requires(valid_visitor<const Choice &&, Visitor>())
    constexpr decltype(auto)
    visit(Visitor&& visitor) const&& noexcept(all_cases_noexcept<0, const Choice&&, Visitor>()) {
        return visit_active<0>(static_cast<const Choice&&>(*this), rstd::forward<Visitor>(visitor));
    }
};

} // namespace rstd
