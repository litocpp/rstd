module;
#define DEFINE_VALID_RANGE_TYPE(name, T, UT, low, high)                    \
    namespace rstd::num::niche_types                                       \
    {                                                                      \
    export struct name : NonZeroBase<T> {                                  \
    public:                                                                \
        using Self = name;                                                 \
        auto make(T in) const noexcept -> Option<Self> {                   \
            constexpr const UT _low { low };                               \
            constexpr const UT _high { high };                             \
            const UT           _in { static_cast<UT>(in) };                \
            if (_in >= _low && _in <= _high) {                             \
                return Some(Self { in });                                  \
            } else {                                                       \
                return None();                                             \
            }                                                              \
        }                                                                  \
        auto new_unchecked(T in) const noexcept -> Self { return { in }; } \
    };                                                                     \
    }

export module rstd.core:num.niche_types;
export import :option_adapter;

namespace rstd::num::niche_types
{

template<typename T>
struct NonZeroBase {
    T val;

    auto as_inner() const noexcept -> T { return val; }
};
} // namespace rstd::num::niche_types

DEFINE_VALID_RANGE_TYPE(NonZeroU8Inner, u8, u8, 1, 0xff)
DEFINE_VALID_RANGE_TYPE(NonZeroU16Inner, u16, u16, 1, 0xffff)
DEFINE_VALID_RANGE_TYPE(NonZeroU32Inner, u32, u32, 1, 0xffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroU64Inner, u64, u64, 1, 0xffffffffffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroI8Inner, i8, u8, 1, 0xff)
DEFINE_VALID_RANGE_TYPE(NonZeroI16Inner, i16, u16, 1, 0xffff)
DEFINE_VALID_RANGE_TYPE(NonZeroI32Inner, i32, u32, 1, 0xffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroI64Inner, i64, u64, 1, 0xffffffffffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroCharInner, char, u32, 1, 0x10ffff)

#if __SIZEOF_POINTER__ == 8
DEFINE_VALID_RANGE_TYPE(UsizeNoHighBit, usize, usize, 0, 0x7fffffffffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroUsizeInner, usize, usize, 1, 0xffffffffffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroIsizeInner, isize, usize, 1, 0xffffffffffffffff)
#elif __SIZEOF_POINTER__ == 4
DEFINE_VALID_RANGE_TYPE(UsizeNoHighBit, usize, usize, 0, 0x7fffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroUsizeInner, usize, usize, 1, 0xffffffff)
DEFINE_VALID_RANGE_TYPE(NonZeroIsizeInner, isize, usize, 1, 0xffffffff)
#else
#    error "Unsupported pointer width"
#endif

DEFINE_VALID_RANGE_TYPE(U32NotAllOnes, u32, u32, 0, 0xfffffffe)
DEFINE_VALID_RANGE_TYPE(I32NotAllOnes, i32, u32, 0, 0xfffffffe)
DEFINE_VALID_RANGE_TYPE(U64NotAllOnes, u64, u64, 0, 0xfffffffffffffffe)
DEFINE_VALID_RANGE_TYPE(I64NotAllOnes, i64, u64, 0, 0xfffffffffffffffe)