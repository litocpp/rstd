module;
#include <rstd/macro.hpp>
export module rstd.alloc:ffi.c_str;
export import :boxed;
export import :vec;

using ::alloc::Allocator;
using ::alloc::boxed::Box;
using ::alloc::vec::Vec;

using rstd::ffi::CStr;
using namespace rstd;

namespace alloc::ffi
{
struct NulError {
    usize                 size;
    ::alloc::vec::Vec<u8> data;
};

export class CString {
public:
    ::alloc::boxed::Box<u8[]> inner;

    USE_TRAIT(CString)

    static auto from_vec_unchecked(::alloc::vec::Vec<u8>&& v) -> Self {
        return _from_vec_unchecked(rstd::move(v));
    }
    static auto _from_vec_unchecked(::alloc::vec::Vec<u8>&& v) -> Self {
        v.push(0);
        auto boxed = v.into_boxed_slice();
        return Self { rstd::move(boxed) };
    }
    template<Impled<convert::Into<::alloc::vec::Vec<u8>>> T>
    static auto make(T t) -> Result<CString, NulError> {
        ::alloc::vec::Vec<u8> vec = rstd::into(rstd::move(t));
        if (auto mem = memchr::memchr(0, vec.as_slice()); mem.is_some()) {
            return Err(NulError { vec.len(), rstd::move(vec) });
        } else {
            return Ok(CString::_from_vec_unchecked(rstd::move(vec)));
        }
    }

    static auto from_raw_parts(char const* p) -> CString {
        auto len    = char_traits<char>::length(p) + 1;
        auto layout = Layout::array<u8>(len).unwrap();
        auto res    = as<Allocator>(GLOBAL).allocate(layout);
        if (res.is_err()) handle_alloc_error(layout);

        auto* raw = static_cast<u8*>(res.unwrap_unchecked().as_mut_ptr().as_raw_ptr());
        rstd::memcpy(raw, p, len);
        auto boxed = ::alloc::boxed::Box<u8[]>::from_raw(mut_ptr<u8[]>::from_raw_parts(raw, len));
        return CString { rstd::move(boxed) };
    }

    auto as_ref() const -> ref<CStr> {
        auto ptr = inner.as_ptr();
        auto p   = reinterpret_cast<CStr const*>(&*ptr);
        return { .p = p, .length = ptr.len() - 1 };
    }

    auto into_bytes() -> ::alloc::vec::Vec<u8> {
        ::alloc::vec::Vec<u8> vec = rstd::into(rstd::move(inner));
        (void)vec.pop(); // remove trailing null byte
        return vec;
    }

    auto to_bytes() const -> slice<u8> {
        auto bytes = to_bytes_with_nul();
        return slice<u8>::from_raw_parts(bytes.p, bytes.len() - 1);
    }

    auto to_bytes_with_nul() const -> slice<u8> {
        auto cstr = as_ref();
        return slice<u8>::from_raw_parts(as_cast<const u8*>(cstr.p), cstr.length);
    }
};

} // namespace alloc::ffi

namespace rstd
{

template<>
struct Impl<clone::Clone, ::alloc::ffi::CString>
    : ImplDefault<clone::Clone, ::alloc::ffi::CString> {
    auto clone() -> ::alloc::ffi::CString {
        return ::alloc::ffi::CString { this->self().inner.clone() };
    }
};

template<mtp::same_as<convert::AsRef<ffi::CStr>> T, mtp::same_as<::alloc::ffi::CString> A>
struct Impl<T, A> {};

} // namespace rstd

template<>
struct rstd::fmt::formatter<::alloc::ffi::CString> : rstd::fmt::formatter<rstd::ref<rstd::str>> {
    template<typename FmtContext>
    auto format(const ::alloc::ffi::CString& cstr, FmtContext& ctx) const -> FmtContext::iterator {
        using namespace rstd;
        auto str_ref = cstr.as_ref();
        auto s       = ref<str>::from_raw_parts((u8 const*)str_ref.p, str_ref.length);
        return fmt::formatter<ref<str>>::format(s, ctx);
    }
};

template<>
struct rstd::fmt::formatter<::alloc::ffi::NulError> : rstd::fmt::formatter<rstd::ref<rstd::str>> {
    template<typename FmtContext>
    auto format(const ::alloc::ffi::NulError& cstr, FmtContext& ctx) const -> FmtContext::iterator {
        using namespace rstd;
        return fmt::formatter<ref<str>>::format("NulError", ctx);
    }
};
