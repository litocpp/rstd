export module rstd.alloc:ffi.c_str;
export import :boxed;
export import :vec;

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
    using Self = CString;
    template<typename, typename>
    friend class Impl;

    CString(const CString&) = delete;
    CString(CString&&)      = default;
    ~CString() noexcept     = default;

    template<Impled<convert::Into<Vec<u8>>> T>
    static auto make(T t) -> Result<CString, NulError> {
        Vec<u8> vec = rstd::into(rstd::move(t));
        if (auto mem = memchr::memchr(0, vec.as_slice()); mem.is_some()) {
            return Err(NulError { vec.len(), rstd::move(vec) });
        } else {
            auto boxed = boxed::Box<u8[]>::from_raw({});
            return Ok(CString { rstd::move(boxed) });
        }
    }

    // static auto _from_vec_unchecked(Vec<u8> v) -> Self {
    //     // v.reserve_exact(1);
    //     // v.push(0);
    // }
};

} // namespace rstd::alloc::ffi