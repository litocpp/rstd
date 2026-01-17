export module rstd.core:ffi.c_str;
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
        return *rstd::bit_cast<CStr const*>(&in.p);
    }
};
static_assert(meta::transparent<CStr*, char*>);

} // namespace rstd::ffi