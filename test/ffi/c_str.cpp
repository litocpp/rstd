#include <gtest/gtest.h>

import rstd;

using namespace rstd::literals;
using rstd::ffi::CStr;
using rstd::ffi::FromBytesWithNulError;
using rstd::ffi::CString;

static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::ref<CStr>>().as_ptr()), char const*>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<CString const&>().as_ptr()), char const*>);

namespace
{

template<rstd::size_t N>
constexpr auto chars(const char (&value)[N]) -> rstd::slice<char> {
    return rstd::slice<char>::from_raw_parts(value, rstd::usize(N));
}

auto bytes(rstd::slice<rstd::u8> value) -> rstd::vec::Vec<rstd::u8> {
    return rstd::vec::Vec<rstd::u8>::from(value);
}

} // namespace

TEST(CStr, CheckedBorrowRequiresOneTrailingNul) {
    auto value = CStr::from_chars_with_nul(chars("hello"));
    ASSERT_TRUE(value.is_ok());
    auto borrowed = value.unwrap_unchecked();

    EXPECT_EQ(borrowed.count_bytes(), rstd::usize(5));
    EXPECT_EQ(borrowed.to_bytes(), "hello"_bytes);
    EXPECT_EQ(borrowed.to_bytes_with_nul(), "hello\0"_bytes);
    EXPECT_EQ(borrowed.as_ptr()[5], '\0');
    EXPECT_EQ(borrowed.to_str().unwrap(), "hello"_str);
}

TEST(CStr, CheckedBorrowReportsInteriorAndMissingNul) {
    char interior[] { 'a', '\0', 'b', '\0' };
    auto interior_result =
        CStr::from_chars_with_nul(rstd::slice<char>::from_raw_parts(interior, rstd::usize(4)));
    ASSERT_TRUE(interior_result.is_err());
    auto interior_error = interior_result.unwrap_err_unchecked();
    EXPECT_EQ(interior_error.kind(), FromBytesWithNulError::Kind::InteriorNul);
    EXPECT_EQ(interior_error.nul_position(), rstd::Some(rstd::usize(1)));

    char missing[] { 'a', 'b' };
    auto missing_result =
        CStr::from_chars_with_nul(rstd::slice<char>::from_raw_parts(missing, rstd::usize(2)));
    ASSERT_TRUE(missing_result.is_err());
    EXPECT_EQ(missing_result.unwrap_err_unchecked().kind(),
              FromBytesWithNulError::Kind::NotNulTerminated);
}

TEST(CString, OwnsCharStorageAndRoundTripsBytes) {
    auto value = CString::make(bytes("\x61\xff\x62"_bytes)).unwrap_unchecked();

    EXPECT_EQ(value.to_bytes(), "\x61\xff\x62"_bytes);
    EXPECT_EQ(value.to_bytes_with_nul(), "\x61\xff\x62\0"_bytes);
    EXPECT_EQ(value.as_ptr()[3], '\0');

    auto recovered = rstd::move(value).into_bytes();
    EXPECT_EQ(recovered.as_slice(), "\x61\xff\x62"_bytes);
}

TEST(CString, ConstructionErrorsRetainInputOwner) {
    auto result = CString::make(bytes("a\0b"_bytes));
    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err_unchecked();
    EXPECT_EQ(error.nul_position(), rstd::usize(1));
    EXPECT_EQ(rstd::move(error).into_vec().as_slice(), "a\0b"_bytes);

    auto with_nul = CString::from_vec_with_nul(bytes("abc"_bytes));
    ASSERT_TRUE(with_nul.is_err());
    auto with_nul_error = rstd::move(with_nul).unwrap_err_unchecked();
    EXPECT_EQ(with_nul_error.error().kind(), FromBytesWithNulError::Kind::NotNulTerminated);
    EXPECT_EQ(rstd::move(with_nul_error).into_bytes().as_slice(), "abc"_bytes);
}

TEST(CString, IntoStringRetainsInvalidCString) {
    auto value  = CString::make(bytes("\xff"_bytes)).unwrap_unchecked();
    auto result = rstd::move(value).into_string();
    ASSERT_TRUE(result.is_err());

    auto error = rstd::move(result).unwrap_err_unchecked();
    EXPECT_EQ(error.utf8_error().valid_up_to(), rstd::usize());
    EXPECT_EQ(rstd::move(error).into_cstring().to_bytes(), "\xff"_bytes);
}
