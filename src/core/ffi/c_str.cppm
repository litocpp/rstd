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

export class CStr {
public:
    CStr()  = delete;
    ~CStr() = delete;

    static auto from_ptr(char const* ptr) noexcept -> CStr const& {
        return *rstd::bit_cast<CStr const*>(ptr);
    }
    static auto from_bytes_until_nul(slice<u8> in) noexcept -> CStr const& {
        return *rstd::bit_cast<CStr const*>(in.p);
    }
};
static_assert(meta::transparent<CStr*, char*>);

} // namespace rstd::ffi
using rstd::ffi::CStr;
namespace rstd
{

template<>
struct Impl<Sized, CStr> {
    ~Impl() = delete;
};

template<>
struct ref<CStr> : ref_base<ref<CStr>, CStr[], false> {
    CStr const* p { nullptr };
    usize       length { 1 };
};

template<>
struct mut_ref<CStr> : ref_base<ref<CStr>, CStr[], true> {
    CStr* p { nullptr };
    usize length { 1 };
};

} // namespace rstd