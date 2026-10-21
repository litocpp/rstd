#include <rstd/test/gtest.hpp>

import rstd.ffi;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::ffi::CStr;
using rstd::ffi::CString;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::ffi::NulError;
using rstd::ffi::FromVecWithNulError;
using rstd::ffi::IntoStringError;

static_assert(rstd::mtp::same_as<NulError, ::alloc::ffi::NulError>);
static_assert(rstd::mtp::same_as<FromVecWithNulError, ::alloc::ffi::FromVecWithNulError>);
static_assert(rstd::mtp::same_as<IntoStringError, ::alloc::ffi::IntoStringError>);

TEST(FfiModule, CStringErrorsRetainOwners) {
    Result<CString, NulError> interior = CString::make(::alloc::vec::Vec<u8>::from("a\0b"_bytes));
    ASSERT_TRUE(interior.is_err());
    auto interior_error = rstd::move(interior).unwrap_err();
    EXPECT_EQ(interior_error.nul_position(), usize(1));
    EXPECT_EQ(rstd::move(interior_error).into_vec().as_slice(), "a\0b"_bytes);

    Result<CString, FromVecWithNulError> missing =
        CString::from_vec_with_nul(::alloc::vec::Vec<u8>::from("abc"_bytes));
    ASSERT_TRUE(missing.is_err());
    auto missing_error = rstd::move(missing).unwrap_err();
    EXPECT_EQ(rstd::move(missing_error).into_bytes().as_slice(), "abc"_bytes);

    auto invalid = CString::make(::alloc::vec::Vec<u8>::from("\xff"_bytes)).unwrap();
    Result<::alloc::string::String, IntoStringError> utf8 = rstd::move(invalid).into_string();
    ASSERT_TRUE(utf8.is_err());
    auto utf8_error = rstd::move(utf8).unwrap_err();
    EXPECT_EQ(utf8_error.utf8_error().valid_up_to(), usize());
    EXPECT_EQ(rstd::move(utf8_error).into_cstring().to_bytes(), "\xff"_bytes);

    Result<ref<CStr>, rstd::ffi::FromBytesUntilNulError> empty =
        CStr::from_chars_until_nul(slice<char> {});
    EXPECT_TRUE(empty.is_err());
}

TEST(FfiModule, DirectImport) {
    auto       owned = OsString::from("host"_str);
    ref<OsStr> view  = owned.as_os_str();
    EXPECT_EQ(view.to_str().unwrap(), "host"_str);
    auto      c_string = CString::make(::alloc::vec::Vec<u8>::from("host"_bytes)).unwrap();
    ref<CStr> c_view   = c_string.as_ref();
    EXPECT_EQ(c_view.to_bytes(), "host"_bytes);
}
