module;
#include <rstd/macro.hpp>
export module rstd.core:ffi.c_str;
import :num.types;
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

/// A borrowed reference to a nul-terminated C string.
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
struct ref<CStr> {
    USE_TRAIT(ref)

    using Target = CStr;

    char const* p { nullptr };
    usize       length { rstd::size_t(1) };

    auto count_bytes() const noexcept { return length; }
    auto is_empty() const noexcept { return length == usize(); }
    auto as_ptr() const noexcept [[clang::lifetimebound]] -> char const* { return p; }
    auto as_raw_ptr() const noexcept [[clang::lifetimebound]] -> char const* { return p; }
    auto metadata() const noexcept { return length; }

    static auto from_raw_parts(char const* p [[clang::lifetimebound]], usize length) noexcept
        -> ref<CStr> {
        return { .p = p, .length = length };
    }

    constexpr auto deref() const noexcept -> ref<Target> { return *this; }
};

template<>
struct mut_ref<CStr> {
    USE_TRAIT(mut_ref)

    using Target = CStr;

    char* p { nullptr };
    usize length { rstd::size_t(1) };

    auto count_bytes() const noexcept { return length; }
    auto is_empty() const noexcept { return length == usize(); }
    auto as_ptr() const noexcept [[clang::lifetimebound]] -> char const* { return p; }
    auto as_mut_ptr() noexcept [[clang::lifetimebound]] -> char* { return p; }
    auto as_raw_ptr() const noexcept [[clang::lifetimebound]] -> char* { return p; }
    auto metadata() const noexcept { return length; }

    static auto from_raw_parts(char* p [[clang::lifetimebound]], usize length) noexcept
        -> mut_ref<CStr> {
        return { .p = p, .length = length };
    }

    constexpr auto deref() const noexcept -> ref<Target> { return { .p = p, .length = length }; }
    constexpr auto deref_mut() noexcept -> mut_ref<Target> { return *this; }
};

namespace ffi
{
class CStr {
public:
    CStr()  = delete;
    ~CStr() = delete;

    static auto from_ptr(char const* p [[clang::lifetimebound]]) noexcept -> ref<CStr> {
        return ref<CStr>::from_raw_parts(p, usize(rstd::strlen(p)));
    }
};

} // namespace ffi

} // namespace rstd
