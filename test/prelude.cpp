import rstd;

using namespace rstd::prelude;

static_assert(rstd::mtp::same_as<rstd::size_t, decltype(sizeof(0))>);
static_assert(
    rstd::mtp::same_as<rstd::ptrdiff_t,
                       decltype(static_cast<char*>(nullptr) - static_cast<char*>(nullptr))>);
static_assert(rstd::mtp::same_as<rstd::int8_t, __INT8_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::int16_t, __INT16_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::int32_t, __INT32_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::int64_t, __INT64_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::uint8_t, __UINT8_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::uint16_t, __UINT16_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::uint32_t, __UINT32_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::uint64_t, __UINT64_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::intptr_t, __INTPTR_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::uintptr_t, __UINTPTR_TYPE__>);
static_assert(rstd::mtp::same_as<rstd::int128_t, __int128>);
static_assert(rstd::mtp::same_as<rstd::uint128_t, unsigned __int128>);
static_assert(rstd::mtp::same_as<rstd::byte, rstd::uint8_t>);
static_assert(rstd::mtp::same_as<rstd::byte, unsigned char>);

static_assert(rstd::mtp::same_as<Box<int>, rstd::boxed::Box<int>>);
static_assert(rstd::mtp::same_as<String, rstd::string::String>);
static_assert(rstd::mtp::same_as<ToString, rstd::string::ToString>);
static_assert(rstd::mtp::same_as<Vec<int>, rstd::vec::Vec<int>>);
static_assert(rstd::mtp::same_as<FnOnce<void()>, rstd::FnOnce<void()>>);
static_assert(rstd::mtp::same_as<Future<int>, rstd::future::Future<int>>);
static_assert(rstd::mtp::same_as<IntoFuture, rstd::async::IntoFuture>);
