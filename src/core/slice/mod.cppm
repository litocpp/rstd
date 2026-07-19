export module rstd.core:slice;
import :num.types;
export import :core;

template<typename T, typename Compare>
constexpr void slice_sift_down(T* data, rstd::size_t root, rstd::size_t end, Compare& compare) {
    while (root * 2 + 1 < end) {
        auto child = root * 2 + 1;
        if (child + 1 < end && compare(data[child], data[child + 1])) ++child;
        if (! compare(data[root], data[child])) return;

        T value     = rstd::move(data[root]);
        data[root]  = rstd::move(data[child]);
        data[child] = rstd::move(value);
        root        = child;
    }
}

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

export namespace rstd::slice_
{

template<typename T, typename Compare>
constexpr void sort_unstable_by(mut_ref<T[]> values, Compare compare) {
    auto const length = values.len().to_primitive();
    if (length < 2) return;

    auto* data = values.as_raw_ptr();
    for (auto root = length / 2; root > 0; --root) {
        slice_sift_down(data, root - 1, length, compare);
    }
    for (auto end = length; end > 1; --end) {
        T value       = rstd::move(data[0]);
        data[0]       = rstd::move(data[end - 1]);
        data[end - 1] = rstd::move(value);
        slice_sift_down(data, 0, end - 1, compare);
    }
}

template<typename T>
constexpr void sort_unstable(mut_ref<T[]> values)
    requires requires(const T& left, const T& right) {
        { left < right } -> mtp::same_as<bool>;
    }
{
    sort_unstable_by(values, [](const T& left, const T& right) {
        return left < right;
    });
}

} // namespace rstd::slice_
