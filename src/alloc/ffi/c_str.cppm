module;
#include <rstd/macro.hpp>
export module rstd.alloc.ffi:c_str;
export import rstd.alloc.boxed;
export import rstd.alloc.vec;

using rstd::alloc::vec::Vec;
using rstd::ffi::CStr;

namespace rstd::alloc::ffi
{
struct NulError {
    usize   size;
    Vec<u8> data;
};

export class CString {
    boxed::Box<u8[]> inner;

    CString() = delete;
    explicit CString(boxed::Box<u8[]>&& b) noexcept: inner(rstd::move(b)) {}

public:
    USE_TRAIT(CString)

    CString(const CString&)                = delete;
    CString(CString&&) noexcept            = default;
    CString& operator=(CString&&) noexcept = default;
    ~CString() noexcept                    = default;

    static auto from_vec_unchecked(Vec<u8>&& v) -> Self {
        debug_assert(memchr::memchr(0, v.as_slice()).is_none());
        return _from_vec_unchecked(rstd::move(v));
    }
    static auto _from_vec_unchecked(Vec<u8>&& v) -> Self {
        v.push(0);
        auto boxed = v.into_boxed_slice();
        return Self { rstd::move(boxed) };
    }
    template<Impled<convert::Into<Vec<u8>>> T>
    static auto make(T t) -> Result<CString, NulError> {
        Vec<u8> vec = rstd::into(rstd::move(t));
        if (auto mem = memchr::memchr(0, vec.as_slice()); mem.is_some()) {
            return Err(NulError { vec.len(), rstd::move(vec) });
        } else {
            return Ok(CString::_from_vec_unchecked(rstd::move(vec)));
        }
    }

    static auto from_raw(char* p) -> CString {
        auto len   = char_traits<char>::length(p) + 1;
        auto boxed = boxed::Box<u8[]>::from_raw(ptr<u8[]>::from_raw(reinterpret_cast<u8*>(p), len));
        return CString { rstd::move(boxed) };
    }

    auto as_ref() const -> ref<const CStr> {
        auto ptr = inner.as_ptr();
        auto p   = reinterpret_cast<CStr const*>(&*ptr);
        return { .p = p, .length = ptr.len() - 1 };
    }

    auto into_bytes() -> Vec<u8> {
        Vec<u8>               vec = rstd::into(rstd::move(inner));
        [[maybe_unused]] auto nul = vec.pop(); // remove trailing null byte
        debug_assert_eq(nul, Some(0));
        return vec;
    }

    auto to_bytes() const -> slice<const u8> {
        auto bytes = to_bytes_with_nul();
        return slice<const u8>::from_raw(*bytes, bytes.len() - 1);
    }

    auto to_bytes_with_nul() const -> slice<const u8> {
        auto cstr = as_ref();
        return slice<const u8>::from_raw(*as_cast<const u8*>(cstr.p), cstr.length);
    }
};

} // namespace rstd::alloc::ffi

using rstd::alloc::ffi::CString;
using rstd::alloc::ffi::NulError;

namespace rstd
{

template<meta::same_as<convert::AsRef<ffi::CStr>> T, meta::same_as<CString> A>
struct Impl<T, A> {};

} // namespace rstd

template<>
struct rstd::fmt::formatter<CString> : rstd::fmt::formatter<rstd::ref<rstd::str>> {
    template<typename FmtContext>
    auto format(const CString& cstr, FmtContext& ctx) const -> FmtContext::iterator {
        // auto sv = cstr.to_string_view_bytes();
        // return rstd::fmt::formatter<rstd::ref<rstd::str>>::format(sv, ctx);
        return rstd::fmt::formatter<rstd::ref<rstd::str>>::format("", ctx);
    }
};

template<>
struct rstd::fmt::formatter<NulError> : rstd::fmt::formatter<rstd::ref<rstd::str>> {
    template<typename FmtContext>
    auto format(const NulError& cstr, FmtContext& ctx) const -> FmtContext::iterator {
        return rstd::fmt::formatter<rstd::ref<rstd::str>>::format("", ctx);
    }
};