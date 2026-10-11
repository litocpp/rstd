export module rstd.core:stream.sources;
export import :stream.traits;
import :iter.sources;

namespace rstd::stream
{

template<iter::has_next I>
struct Iter {
    using Item = typename I::Item;
    I    iterator;
    auto poll_next(mut_ref<Iter> self, task::Context&) -> Poll<Option<Item>> {
        return Poll<Option<Item>>::Ready(self->iterator.next());
    }
};

export template<typename I>
    requires(! mtp::is_ref<I>) && iter::into_iterable<I>
auto iter(I&& source) {
    auto iterator = rstd::iter::into_iter(rstd::forward<I>(source));
    return Iter<decltype(iterator)> { rstd::move(iterator) };
}

export template<rstd::iter::valid_item T>
    requires(! mtp::is_void<T>)
auto empty() {
    return stream::iter(rstd::iter::empty<T>());
}

export template<typename T>
auto once(T value) {
    return stream::iter(rstd::iter::once(rstd::move(value)));
}

} // namespace rstd::stream
