export module rstd.core:f64_;
export import rstd.basic;

export namespace rstd::f64_
{

/// Radix of the exponent representation.
inline constexpr u32 RADIX = 2;

/// Number of significand bits including the implicit leading one.
inline constexpr u32 MANTISSA_DIGITS = 53;

/// Number of base-10 digits the type can represent without loss.
inline constexpr u32 DIGITS = 15;

/// One greater than the minimum possible normal power-of-2 exponent.
inline constexpr i32 MIN_EXP = -1021;

/// Maximum possible power-of-2 exponent.
inline constexpr i32 MAX_EXP = 1024;

/// Minimum possible normal power-of-10 exponent.
inline constexpr i32 MIN_10_EXP = -307;

/// Maximum possible power-of-10 exponent.
inline constexpr i32 MAX_10_EXP = 308;

/// Difference between `1.0` and the next larger representable number.
inline constexpr f64 EPSILON = numeric_limits<f64>::epsilon();

/// Smallest positive normal value.
inline constexpr f64 MIN_POSITIVE = numeric_limits<f64>::min();

/// Smallest finite value.
inline constexpr f64 MIN = numeric_limits<f64>::lowest();

/// Largest finite value.
inline constexpr f64 MAX = numeric_limits<f64>::max();

/// Positive infinity.
inline constexpr f64 INFINITY_ = numeric_limits<f64>::infinity();

/// Negative infinity.
inline constexpr f64 NEG_INFINITY = -numeric_limits<f64>::infinity();

/// Not a Number (NaN).
inline constexpr f64 NAN_ = numeric_limits<f64>::quiet_NaN();

namespace consts
{

/// Archimedes' constant (π).
inline constexpr f64 PI = 3.14159265358979323846264338327950288;

/// The full circle constant (τ). Equal to 2π.
inline constexpr f64 TAU = 6.28318530717958647692528676655900577;

/// π/2.
inline constexpr f64 FRAC_PI_2 = 1.57079632679489661923132169163975144;

/// π/3.
inline constexpr f64 FRAC_PI_3 = 1.04719755119659774615421446109316763;

/// π/4.
inline constexpr f64 FRAC_PI_4 = 0.785398163397448309615660845819875721;

/// π/6.
inline constexpr f64 FRAC_PI_6 = 0.52359877559829887307710723054658381;

/// π/8.
inline constexpr f64 FRAC_PI_8 = 0.39269908169872415480783042290993786;

/// 1/π.
inline constexpr f64 FRAC_1_PI = 0.318309886183790671537767526745028724;

/// 2/π.
inline constexpr f64 FRAC_2_PI = 0.636619772367581343075535053490057448;

/// 2/sqrt(π).
inline constexpr f64 FRAC_2_SQRT_PI = 1.12837916709551257389615890312154517;

/// sqrt(2).
inline constexpr f64 SQRT_2 = 1.41421356237309504880168872420969808;

/// 1/sqrt(2).
inline constexpr f64 FRAC_1_SQRT_2 = 0.707106781186547524400844362104849039;

/// Euler's number (e).
inline constexpr f64 E = 2.71828182845904523536028747135266250;

/// log_2(10).
inline constexpr f64 LOG2_10 = 3.32192809488736234787031942948939018;

/// log_2(e).
inline constexpr f64 LOG2_E = 1.44269504088896340735992468100189214;

/// log_10(2).
inline constexpr f64 LOG10_2 = 0.301029995663981195213738894724493027;

/// log_10(e).
inline constexpr f64 LOG10_E = 0.434294481903251827651128918916605082;

/// ln(2).
inline constexpr f64 LN_2 = 0.693147180559945309417232121458176568;

/// ln(10).
inline constexpr f64 LN_10 = 2.30258509299404568401799145468436421;

} // namespace consts

} // namespace rstd::f64_
