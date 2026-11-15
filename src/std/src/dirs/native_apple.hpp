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

// The receiver must copy each path before returning; Foundation owns the bytes.
auto query_apple_roots(void* context, AppleRootReceiver receive) -> bool;

} // namespace rstd::dirs::sys
