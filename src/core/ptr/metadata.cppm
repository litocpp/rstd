export module rstd.core:ptr.metadata;
export import :trait;

// https://github.com/rust-lang/rfcs/blob/master/text/2580-ptr-meta.md

namespace rstd
{

namespace ptr_
{
struct Pointee {
    using Metadata = void;
};
} // namespace ptr_

template<mtp::same_as<ptr_::Pointee> T, typename A>
struct Impl<T, A[]> {
    using Metadata = usize;
};

namespace mtp
{

template<typename T>
concept DST = mtp::destructible<Impl<ptr_::Pointee, T>>;

template<typename T>
concept DSTArray = mtp::same_as<typename Impl<ptr_::Pointee, T>::Metadata, usize>;

} // namespace mtp

} // namespace rstd