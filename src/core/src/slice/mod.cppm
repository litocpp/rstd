export module rstd.core:slice;
import :num.types;
export import :core;

template<typename T, typename Compare>
constexpr void
slice_sift_down(rstd::mut_ref<T[]> data, rstd::size_t root, rstd::size_t end, Compare& compare) {
    while (root * 2 + 1 < end) {
        auto child = root * 2 + 1;
        if (child + 1 < end && compare(data[rstd::usize(child)], data[rstd::usize(child + 1)])) {
            ++child;
        }
        if (! compare(data[rstd::usize(root)], data[rstd::usize(child)])) return;

        auto pointer = rstd::mut_ptr<T>::from_raw_parts(data.as_raw_ptr());
        rstd::ptr_::swap(pointer.add(rstd::usize(root)), pointer.add(rstd::usize(child)));
        root = child;
    }
}

namespace rstd
{

export constexpr auto as_bytes(slice<u8> values [[clang::lifetimebound]]) noexcept -> slice<byte> {
    if (values.is_empty()) return {};
    return slice<byte>::from_raw_parts(values.as_raw_ptr(), values.len());
}

export constexpr auto as_bytes_mut(mut_ref<u8[]> values [[clang::lifetimebound]]) noexcept
    -> mut_ref<byte[]> {
    if (values.is_empty()) return {};
    return mut_ref<byte[]>::from_raw_parts(values.as_raw_ptr(), values.len());
}

export constexpr auto as_u8_slice(slice<byte> values [[clang::lifetimebound]]) noexcept
    -> slice<u8> {
    if (values.is_empty()) return {};
    return slice<u8>::from_raw_parts(values.as_raw_ptr(), values.len());
}

export constexpr auto as_u8_slice_mut(mut_ref<byte[]> values [[clang::lifetimebound]]) noexcept
    -> mut_ref<u8[]> {
    if (values.is_empty()) return {};
    return mut_ref<u8[]>::from_raw_parts(values.as_raw_ptr(), values.len());
}

} // namespace rstd

export namespace rstd::slice_
{

template<typename T, typename Compare>
constexpr void sort_unstable_by(mut_ref<T[]> values, Compare compare) {
    auto const length = values.len().to_primitive();
    if (length < 2) return;

    for (auto root = length / 2; root > 0; --root) {
        slice_sift_down(values, root - 1, length, compare);
    }
    for (auto end = length; end > 1; --end) {
        auto pointer = mut_ptr<T>::from_raw_parts(values.as_raw_ptr());
        ptr_::swap(pointer, pointer.add(usize(end - 1)));
        slice_sift_down(values, 0, end - 1, compare);
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

template<typename T, typename F>
constexpr void sort_unstable_by_key(mut_ref<T[]> values, F key) {
    sort_unstable_by(values, [&key](const T& a, const T& b) {
        return key(a) < key(b);
    });
}

} // namespace rstd::slice_
