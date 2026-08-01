export module rstd.core:forward;

namespace rstd::result
{
export struct UnknownOk;
export struct UnknownErr;

export template<typename T, typename E>
class Result;

export template<typename T, typename TErr = UnknownErr>
constexpr auto Ok(T&& val) -> Result<T, TErr>;

export template<typename TErr, typename T = UnknownOk>
constexpr auto Err(TErr&& val) -> Result<T, TErr>;
} // namespace rstd::result