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

    static auto from_raw_parts(char const* p) -> CString {
        auto len = char_traits<char>::length(p) + 1;
        auto raw = new u8[len];
        rstd::memcpy(raw, p, len);
        auto boxed = boxed::Box<u8[]>::from_raw(mut_ptr<u8[]>::from_raw_parts(raw, len));
        return CString { rstd::move(boxed) };
    }

    auto as_ref() const -> ref<CStr> {
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

    auto to_bytes() const -> slice<u8> {
        auto bytes = to_bytes_with_nul();
        return slice<u8>::from_raw_parts(bytes.p, bytes.len() - 1);
    }

    auto to_bytes_with_nul() const -> slice<u8> {
        auto cstr = as_ref();
        return slice<u8>::from_raw_parts(as_cast<const u8*>(cstr.p), cstr.length);
    }
};

} // namespace rstd::alloc::ffi

using rstd::alloc::ffi::CString;
using rstd::alloc::ffi::NulError;

namespace rstd
{

template<>
struct Impl<clone::Clone, CString> : ImplDefault<clone::Clone, CString> {
    auto clone() -> CString { return CString { this->self().inner.clone() }; }
};

template<meta::same_as<convert::AsRef<ffi::CStr>> T, meta::same_as<CString> A>
struct Impl<T, A> {};

} // namespace rstd

template<>
struct rstd::fmt::formatter<CString> : rstd::fmt::formatter<rstd::ref<rstd::str>> {
    template<typename FmtContext>
    auto format(const CString& cstr, FmtContext& ctx) const -> FmtContext::iterator {
        using namespace rstd;
        auto str_ref = cstr.as_ref();
        ref<str>::from_raw_parts((u8 const*)str_ref.p, str_ref.length);
        return fmt::formatter<ref<str>>::format("", ctx);
    }
};

template<>
struct rstd::fmt::formatter<NulError> : rstd::fmt::formatter<rstd::ref<rstd::str>> {
    template<typename FmtContext>
    auto format(const NulError& cstr, FmtContext& ctx) const -> FmtContext::iterator {
        using namespace rstd;
        return fmt::formatter<ref<str>>::format("NulError", ctx);
    }
};