export module rstd.core:slice.ops;
import :num.types;
export import :slice;
export import :clone;
export import :marker;
import :panicking;
import :ptr;

namespace rstd::slice_::detail
{

[[noreturn]]
inline void split_index_out_of_bounds() {
    rstd::panic { "slice split index out of bounds" };
}

[[noreturn]]
inline void length_mismatch() {
    rstd::panic { "source and destination slices have different lengths" };
}

} // namespace rstd::slice_::detail

export namespace rstd::slice_
{

template<typename T>
constexpr auto split_at_mut(mut_ref<T[]> values [[clang::lifetimebound]], usize mid)
    -> tuple<mut_ref<T[]>, mut_ref<T[]>> {
    if (mid > values.len()) detail::split_index_out_of_bounds();

    auto* data       = values.as_raw_ptr();
    auto* split_data = data;
    if (mid != usize()) split_data += mid.to_primitive();

    return { mut_ref<T[]>::from_raw_parts(data, mid),
             mut_ref<T[]>::from_raw_parts(split_data, values.len() - mid) };
}

template<typename T>
void copy_from_slice(mut_ref<T[]> destination, slice<T> source)
    requires Impled<T, Copy>
{
    if (destination.len() != source.len()) detail::length_mismatch();
    ptr_::copy_nonoverlapping(ptr<T>::from_raw_parts(source.as_raw_ptr()),
                              mut_ptr<T>::from_raw_parts(destination.as_raw_ptr()),
                              source.len());
}

template<typename T>
void clone_from_slice(mut_ref<T[]> destination, slice<T> source)
    requires Impled<T, clone::Clone>
{
    if (destination.len() != source.len()) detail::length_mismatch();

    if constexpr (Impled<T, Copy>) {
        copy_from_slice(destination, source);
    } else {
        for (rstd::size_t index = 0; index < source.len().to_primitive(); ++index) {
            as<clone::Clone>(destination[usize(index)]).clone_from(source[usize(index)]);
        }
    }
}

} // namespace rstd::slice_
