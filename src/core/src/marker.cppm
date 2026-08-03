export module rstd.core:marker;
import :num.types;
export import :clone;
export import :trait;

namespace rstd
{

/// Marker trait for types that can be duplicated by simple bitwise copy.
export struct Copy {
    using SuperTraits = TraitList<clone::Clone>;

    template<typename Self, typename = void>
        requires mtp::triv_copyable<Self> && mtp::triv_copy<Self> && mtp::triv_assign_copy<Self>
    struct Api {
        using Trait = Copy;
    };

    template<typename>
    using Funcs = TraitFuncs<>;
};

/// Marker trait for types that can be safely transferred across thread boundaries.
export struct Send {};

/// Marker trait for types that can be safely shared between threads via references.
export struct Sync {};

/// Marker trait for types with a constant size known at compile time.
export struct Sized {};

} // namespace rstd

export namespace rstd
{

template<typename T>
struct Impl<Sized, T[]> {
    ~Impl() = delete;
};

template<typename T>
    requires mtp::drop<T>
struct Impl<Sized, T> {};

template<typename T>
    requires num::PrimitiveInteger<T> || num::PrimitiveFloat<T> || mtp::is_ptr<T> ||
             mtp::any<T,
                      bool,
                      char,
                      wchar_t,
                      char8_t,
                      char16_t,
                      char32_t,
                      byte,
                      u8,
                      u16,
                      u32,
                      u64,
                      u128,
                      usize,
                      i8,
                      i16,
                      i32,
                      i64,
                      i128,
                      isize,
                      f32,
                      f64>
struct Impl<Copy, T> {};

template<typename... Ts>
    requires(Impled<Ts, Copy> && ...)
struct Impl<Copy, tuple<Ts...>> {};

static_assert(Impled<i32, Sized>);
static_assert(! Impled<i32[], Sized>);

} // namespace rstd
