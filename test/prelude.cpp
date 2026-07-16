import rstd;

using namespace rstd::prelude;

static_assert(rstd::mtp::same_as<Box<int>, rstd::boxed::Box<int>>);
static_assert(rstd::mtp::same_as<String, rstd::string::String>);
static_assert(rstd::mtp::same_as<ToString, rstd::string::ToString>);
static_assert(rstd::mtp::same_as<Vec<int>, rstd::vec::Vec<int>>);
static_assert(rstd::mtp::same_as<FnOnce<void()>, rstd::FnOnce<void()>>);
static_assert(rstd::mtp::same_as<Future<int>, rstd::future::Future<int>>);
static_assert(rstd::mtp::same_as<IntoFuture, rstd::async::IntoFuture>);
