export module rstd.core:slice;
import :num.types;
export import :core;

namespace rstd
{

static_assert(mtp::same_as<byte, uint8_t>);
static_assert(mtp::same_as<byte, unsigned char>);

export constexpr auto byte_value(byte value) noexcept -> u8 {
    return u8(value);
}

export constexpr auto as_bytes(slice<u8> values [[clang::lifetimebound]]) noexcept -> slice<byte> {
    if (values.is_empty()) return {};
    return slice<byte>::from_raw_parts(reinterpret_cast<byte const*>(values.as_raw_ptr()),
                                       values.len());
}

export constexpr auto as_bytes_mut(mut_ref<u8[]> values [[clang::lifetimebound]]) noexcept
    -> mut_ref<byte[]> {
    if (values.is_empty()) return {};
    return mut_ref<byte[]>::from_raw_parts(reinterpret_cast<byte*>(values.as_raw_ptr()),
                                           values.len());
}

export class U8Values {
    slice<byte> values_ {};

public:
    class Iterator {
        byte const* current_ { nullptr };

    public:
        explicit constexpr Iterator(byte const* current) noexcept: current_(current) {}
        constexpr auto operator*() const noexcept -> u8 { return byte_value(*current_); }
        constexpr auto operator++() noexcept -> Iterator& {
            ++current_;
            return *this;
        }
        constexpr auto operator==(Iterator const&) const noexcept -> bool = default;
    };

    explicit constexpr U8Values(slice<byte> values [[clang::lifetimebound]]) noexcept
        : values_(values) {}

    constexpr auto len() const noexcept -> usize { return values_.len(); }
    constexpr auto is_empty() const noexcept -> bool { return values_.is_empty(); }
    constexpr auto operator[](usize index) const noexcept -> u8 {
        return byte_value(values_[index]);
    }
    constexpr auto begin() const noexcept [[clang::lifetimebound]] -> Iterator {
        return Iterator { values_.as_raw_ptr() };
    }
    constexpr auto end() const noexcept [[clang::lifetimebound]] -> Iterator {
        if (values_.is_empty()) return Iterator { values_.as_raw_ptr() };
        return Iterator { values_.as_raw_ptr() + values_.len().to_primitive() };
    }
};

export constexpr auto u8_values(slice<byte> values [[clang::lifetimebound]]) noexcept -> U8Values {
    return U8Values(values);
}

} // namespace rstd
