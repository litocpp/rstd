export module rstd.core:ffi.c_str;
export import :marker;

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
struct ref<CStr> : ptr_base<ref<CStr>, CStr[]> {
    CStr* p { nullptr };
    usize length { 1 };
};

template<>
struct ref<const CStr> : ptr_base<ref<const CStr>, CStr[]> {
    CStr const* p { nullptr };
    usize       length { 1 };
};

export template<typename T>
auto as_cast(CStr* p) -> T {
    return reinterpret_cast<T>(p);
}
export template<typename T>
auto as_cast(CStr const* p) -> T {
    return reinterpret_cast<T>(p);
}

} // namespace rstd