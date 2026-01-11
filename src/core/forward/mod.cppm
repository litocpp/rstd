export module rstd.core:forward;

namespace rstd::result
{
export struct UnknownOk;
export struct UnknownErr;

export template<typename T, typename E>
class Result;

export template<typename T>
using Ok = Result<T, UnknownErr>;

export template<typename T>
using Err = Result<UnknownOk, T>;
} // namespace rstd::result