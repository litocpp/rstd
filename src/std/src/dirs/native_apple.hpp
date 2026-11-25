#pragma once

namespace rstd::dirs::sys
{

enum class AppleDirectory
{
    Home,
    ApplicationSupport,
    Cache,
    LocalApplicationSupport
};

using AppleRootReceiver = bool (*)(void*, AppleDirectory, const char*);

// The receiver and its context must stay valid during the call.
// The receiver must copy each path before returning; Foundation owns the bytes.
[[unsafe]]
auto query_apple_roots(void* context, AppleRootReceiver receive) -> bool;

} // namespace rstd::dirs::sys
