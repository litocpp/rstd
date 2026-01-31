export module rstd.core:ffi.c_str;
export import :marker;
export import :core;

namespace rstd::ffi
{
// enum FromBytesWithNulError {
//     /// Data provided contains an interior nul byte at byte `position`.
//     InteriorNul {
//         /// The position of the interior nul byte.
//         position: usize,
//     },
//     /// Data provided is not nul terminated.
//     NotNulTerminated,
// }

export class CStr;

} // namespace rstd::ffi

using rstd::ffi::CStr;
namespace rstd
{

template<>
struct Impl<Sized, CStr> {
    ~Impl() = delete;
};

template<>
struct Impl<ptr_::Pointee, CStr> {
    using Metadata = usize;
};

template<>
struct ref<CStr> : ref_base<ref<CStr>, CStr, false> {
    CStr const* p { nullptr };
    usize       length { 1 };

    auto count_bytes() const noexcept { return length; }
    auto is_empty() const noexcept { return length == 0; }
    auto to_bytes() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(reinterpret_cast<u8 const*>(p), length);
    }
};

template<>
struct mut_ref<CStr> : ref_base<ref<CStr>, CStr, true> {
    CStr* p { nullptr };
    usize length { 1 };

    auto count_bytes() const noexcept { return length; }
    auto is_empty() const noexcept { return length == 0; }
};

namespace ffi
{
class CStr {
public:
    CStr()  = delete;
    ~CStr() = delete;

    static auto from_ptr(char const* p) noexcept -> ref<CStr> {
        return ref<CStr>::from_raw_parts(reinterpret_cast<CStr const*>(p),
                                         char_traits<char>::length(p));
    }
};

} // namespace ffi

} // namespace rstd