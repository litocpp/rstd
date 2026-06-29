export module rstd.core:enum_;
import :intrinsics;
export import :core;

namespace rstd::enum_detail
{

export [[noreturn]]
inline void bad_enum_state() noexcept {
    rstd::intrinsics::abort();
}

export struct sentinel {};

template<usize N>
struct smallest_index {
    using type = mtp::cond<(N <= 0xff), u8, mtp::cond<(N <= 0xffff), u16, u32>>;
};

export template<usize N>
using smallest_index_t = typename smallest_index<N>::type;

export template<usize I>
struct in_place_index_t {
    explicit constexpr in_place_index_t() = default;
};

export template<usize I>
inline constexpr in_place_index_t<I> in_place_index {};

template<usize I, typename T, typename... Rest>
struct type_at_impl {
    using type = typename type_at_impl<I - 1, Rest...>::type;
};

template<typename T, typename... Rest>
struct type_at_impl<0, T, Rest...> {
    using type = T;
};

template<usize I, typename... Ts>
using type_at_t = typename type_at_impl<I, Ts...>::type;

export template<typename Like, typename T>
[[nodiscard]]
constexpr decltype(auto) forward_like(T&& x) noexcept {
    using U = mtp::rm_ref<T>;
    if constexpr (mtp::is_ref_lv<Like>) {
        if constexpr (mtp::is_const<mtp::rm_ref<Like>>) {
            return static_cast<U const&>(x);
        } else {
            return static_cast<U&>(x);
        }
    } else {
        if constexpr (mtp::is_const<mtp::rm_ref<Like>>) {
            return static_cast<U const&&>(x);
        } else {
            return static_cast<U&&>(x);
        }
    }
}

template<usize Base, typename... Ts>
union union_pack;

template<usize Base>
union union_pack<Base> {
    constexpr union_pack() noexcept {}
    constexpr ~union_pack() noexcept = default;
};

template<usize Base, typename T, typename... Rest>
union union_pack<Base, T, Rest...> {
    T                             head;
    union_pack<Base + 1, Rest...> tail;

    constexpr union_pack() noexcept {}

    constexpr ~union_pack() noexcept
        requires(mtp::triv_drop<T> && (mtp::triv_drop<Rest> && ...))
    = default;

    constexpr ~union_pack() noexcept
        requires(! (mtp::triv_drop<T> && (mtp::triv_drop<Rest> && ...)))
    {}

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() & noexcept {
        if constexpr (I == Base) {
            return (head);
        } else {
            return tail.template get<I>();
        }
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() const& noexcept {
        if constexpr (I == Base) {
            return (head);
        } else {
            return tail.template get<I>();
        }
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() && noexcept {
        if constexpr (I == Base) {
            return static_cast<T&&>(head);
        } else {
            return static_cast<union_pack<Base + 1, Rest...>&&>(tail).template get<I>();
        }
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() const&& noexcept {
        if constexpr (I == Base) {
            return static_cast<T const&&>(head);
        } else {
            return static_cast<union_pack<Base + 1, Rest...> const&&>(tail).template get<I>();
        }
    }
};

export template<typename... Ts>
class storage {
    using index_type = smallest_index_t<sizeof...(Ts)>;

    static constexpr usize      count         = sizeof...(Ts);
    static constexpr index_type invalid_index = static_cast<index_type>(count);

    union_pack<0, Ts...> data_;
    index_type           index_ = invalid_index;

    template<usize I>
    void destroy_impl() noexcept {
        if constexpr (I < count) {
            if (index_ == static_cast<index_type>(I)) {
                rstd::destroy_at(rstd::addressof(data_.template get<I>()));
            } else {
                destroy_impl<I + 1>();
            }
        }
    }

    template<usize I>
    void copy_impl(storage const& other) {
        if constexpr (I < count) {
            if (other.index_ == static_cast<index_type>(I)) {
                construct(in_place_index<I>, other.data_.template get<I>());
            } else {
                copy_impl<I + 1>(other);
            }
        } else {
            bad_enum_state();
        }
    }

    template<usize I>
    void move_impl(storage&& other) noexcept((mtp::noex_move<Ts> && ...)) {
        if constexpr (I < count) {
            if (other.index_ == static_cast<index_type>(I)) {
                construct(in_place_index<I>, rstd::move(other.data_).template get<I>());
            } else {
                move_impl<I + 1>(rstd::move(other));
            }
        } else {
            bad_enum_state();
        }
    }

    template<usize I, typename... Args>
        requires(I < count) && mtp::init<type_at_t<I, Ts...>, Args...>
    constexpr void
    construct(in_place_index_t<I>,
              Args&&... args) noexcept(mtp::noex_init<type_at_t<I, Ts...>, Args...>) {
        rstd::construct_at(rstd::addressof(data_.template get<I>()), rstd::forward<Args>(args)...);
        index_ = static_cast<index_type>(I);
    }

public:
    constexpr storage() noexcept = default;

    template<usize I, typename... Args>
        requires(I < count) && mtp::init<type_at_t<I, Ts...>, Args...>
    explicit constexpr storage(in_place_index_t<I>, Args&&... args) noexcept(
        mtp::noex_init<type_at_t<I, Ts...>, Args...>) {
        construct(in_place_index<I>, rstd::forward<Args>(args)...);
    }

    constexpr storage(storage const& other)
        requires(mtp::copy<Ts> && ...)
        : data_(), index_(invalid_index) {
        copy_impl<0>(other);
    }

    constexpr storage(storage const&)
        requires(! (mtp::copy<Ts> && ...))
    = delete;

    constexpr storage(storage&& other) noexcept((mtp::noex_move<Ts> && ...))
        requires(mtp::move<Ts> && ...)
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

    constexpr storage& operator=(storage const& rhs)
        requires((mtp::copy<Ts> && ...) && (mtp::noex_move<Ts> && ...))
    {
        if (this == rstd::addressof(rhs)) {
            return *this;
        }
        storage tmp(rhs);
        destroy();
        move_impl<0>(rstd::move(tmp));
        return *this;
    }

    constexpr storage& operator=(storage const&)
        requires(! ((mtp::copy<Ts> && ...) && (mtp::noex_move<Ts> && ...)))
    = delete;

    constexpr storage& operator=(storage&& rhs) noexcept((mtp::noex_move<Ts> && ...))
        requires((mtp::move<Ts> && ...) && (mtp::noex_move<Ts> && ...))
    {
        if (this == rstd::addressof(rhs)) {
            return *this;
        }
        destroy();
        move_impl<0>(rstd::move(rhs));
        return *this;
    }

    constexpr storage& operator=(storage&&)
        requires(! ((mtp::move<Ts> && ...) && (mtp::noex_move<Ts> && ...)))
    = delete;

    template<usize I, typename... Args>
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
    constexpr usize index() const noexcept {
        return static_cast<usize>(index_);
    }

    [[nodiscard]]
    constexpr bool valid() const noexcept {
        return index_ != invalid_index;
    }

    template<usize I>
        requires(I < count)
    [[nodiscard]]
    constexpr bool is(in_place_index_t<I>) const noexcept {
        return index_ == static_cast<index_type>(I);
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() & noexcept {
        return data_.template get<I>();
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() const& noexcept {
        return data_.template get<I>();
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() && noexcept {
        return rstd::move(data_).template get<I>();
    }

    template<usize I>
    [[nodiscard]]
    constexpr decltype(auto) get() const&& noexcept {
        return static_cast<union_pack<0, Ts...> const&&>(data_).template get<I>();
    }

    template<usize I>
        requires(I < count)
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) & noexcept {
        return get<I>();
    }

    template<usize I>
        requires(I < count)
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) const& noexcept {
        return get<I>();
    }

    template<usize I>
        requires(I < count)
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) && noexcept {
        return rstd::move(*this).template get<I>();
    }

    template<usize I>
        requires(I < count)
    [[nodiscard]]
    constexpr decltype(auto) get(in_place_index_t<I>) const&& noexcept {
        return static_cast<storage const&&>(*this).template get<I>();
    }
};

} // namespace rstd::enum_detail
