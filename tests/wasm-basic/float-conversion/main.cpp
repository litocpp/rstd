import rstd.core;

extern "C" [[clang::export_name("ldexp_f32")]]
auto ldexp_f32(float value, int exponent) -> float {
    return rstd::f32(value).ldexp(rstd::i32(exponent)).to_primitive();
}

extern "C" [[clang::export_name("ldexp_f64")]]
auto ldexp_f64(double value, int exponent) -> double {
    return rstd::f64(value).ldexp(rstd::i32(exponent)).to_primitive();
}

extern "C" [[clang::export_name("rstd_wasm_float_cast")]]
auto rstd_wasm_float_cast(double value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u32>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("rstd_wasm_float_checked")]]
auto rstd_wasm_float_checked(double value) -> rstd::uint32_t {
    return rstd::uint32_t(rstd::try_from<rstd::u32>(rstd::f64(value)).is_ok());
}

extern "C" [[clang::export_name("rstd_wasm_float_signed")]]
auto rstd_wasm_float_signed(float value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i32>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_i8")]]
auto cast_f32_i8(float value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i8>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_u8")]]
auto cast_f32_u8(float value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u8>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_i16")]]
auto cast_f32_i16(float value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i16>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_u16")]]
auto cast_f32_u16(float value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u16>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_i32")]]
auto cast_f32_i32(float value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i32>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_u32")]]
auto cast_f32_u32(float value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u32>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_i64")]]
auto cast_f32_i64(float value) -> rstd::int64_t {
    return rstd::as_cast<rstd::i64>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f32_u64")]]
auto cast_f32_u64(float value) -> rstd::uint64_t {
    return rstd::as_cast<rstd::u64>(rstd::f32(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_i8")]]
auto cast_f64_i8(double value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i8>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_u8")]]
auto cast_f64_u8(double value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u8>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_i16")]]
auto cast_f64_i16(double value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i16>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_u16")]]
auto cast_f64_u16(double value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u16>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_i32")]]
auto cast_f64_i32(double value) -> rstd::int32_t {
    return rstd::as_cast<rstd::i32>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_u32")]]
auto cast_f64_u32(double value) -> rstd::uint32_t {
    return rstd::as_cast<rstd::u32>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_i64")]]
auto cast_f64_i64(double value) -> rstd::int64_t {
    return rstd::as_cast<rstd::i64>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_u64")]]
auto cast_f64_u64(double value) -> rstd::uint64_t {
    return rstd::as_cast<rstd::u64>(rstd::f64(value)).to_primitive();
}

extern "C" [[clang::export_name("cast_f64_f32")]]
auto cast_f64_f32(double value) -> float {
    return rstd::as_cast<rstd::f32>(value).to_primitive();
}

extern "C" [[clang::export_name("cast_u64_f32")]]
auto cast_u64_f32(rstd::uint64_t value) -> float {
    return rstd::as_cast<rstd::f32>(value).to_primitive();
}

static_assert(rstd::as_cast<rstd::u32>(__builtin_nan("")) == rstd::u32());
static_assert(rstd::as_cast<rstd::i64>(-__builtin_inff()) == rstd::i64::MIN);
static_assert(rstd::as_cast<rstd::u64>(0x1p64) == rstd::u64::MAX);
static_assert(rstd::as_cast<rstd::i32>(-42.75f) == rstd::i32(-42));
