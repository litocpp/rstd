export module rstd.core:ptr.metadata;
export import :trait;

// https://github.com/rust-lang/rfcs/blob/master/text/2580-ptr-meta.md

namespace rstd
{

namespace ptr_
{
export struct Pointee {
    using Metadata = void;
};
} // namespace ptr_

template<mtp::same_as<ptr_::Pointee> T, mtp::is_array A>
struct Impl<T, A> {
    using Metadata = usize;
};

namespace mtp
{

export template<typename T>
concept DST = Impled<T, ptr_::Pointee>;

export template<typename T>
concept DSTArray = mtp::same<typename Impl<ptr_::Pointee, T>::Metadata, usize>;

} // namespace mtp

} // namespace rstd