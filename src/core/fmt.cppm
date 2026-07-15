export module rstd.core:fmt;
export import :trait;
export import rstd.basic;

namespace rstd::fmt

{

/// Forward declaration of the Formatter used during format output.
export struct Formatter;

// Forward declarations consumed by the `Arguments::make` factory below.
// `FormatString` is forward-declared; its definition lives further
// down once the consteval check helpers are in scope.
template<typename T>
struct _FmtId {
    using type = T;
};
export template<typename... Args>
struct FormatString;
export template<typename... Args>
using format_string = FormatString<typename _FmtId<Args>::type...>;

namespace detail
{
template<usize N>
struct ArgumentsStorage;
}

/// Text alignment options for formatted output.
export enum class Align : u32 { None = 0, Left = 1, Right = 2, Center = 3 };

/// Formatting trait selected by the placeholder's type specifier.
export enum class Presentation : u8 { Display, Debug, LowerExp, UpperExp };

/// Options that control how values are formatted (fill, align, width, precision, flags).
///
/// Compact bit layout used by rstd's runtime parser:
///   [20: 0]  fill character (Unicode scalar, default U+0020 ' ')
///   [22:21]  alignment  0=none 1=left 2=right 3=center
///   [23]     sign_plus   (+)
///   [24]     sign_minus  (-)
///   [25]     alternate   (#)
///   [26]     zero_pad    (0)
///   [27]     debug       (?)
///   [28]     has_width
///   [29]     has_precision
///   [30]     lower_exp   (e)
///   [31]     upper_exp   (E)
export struct FormattingOptions {
    u32 flags     = u32(' '); // fill=' ', all flags clear
    u16 width     = 0;
    u16 precision = 0;

    static constexpr u32 FILL_MASK         = 0x1F'FFFFu;
    static constexpr u32 ALIGN_SHIFT       = 21u;
    static constexpr u32 ALIGN_MASK        = 0b11u << 21u;
    static constexpr u32 SIGN_PLUS         = 1u << 23u;
    static constexpr u32 SIGN_MINUS        = 1u << 24u;
    static constexpr u32 ALTERNATE         = 1u << 25u;
    static constexpr u32 ZERO_PAD          = 1u << 26u;
    static constexpr u32 DEBUG             = 1u << 27u;
    static constexpr u32 HAS_WIDTH         = 1u << 28u;
    static constexpr u32 HAS_PREC          = 1u << 29u;
    static constexpr u32 LOWER_EXP         = 1u << 30u;
    static constexpr u32 UPPER_EXP         = 1u << 31u;
    static constexpr u32 PRESENTATION_MASK = DEBUG | LOWER_EXP | UPPER_EXP;

    constexpr auto fill() const noexcept -> char { return char(flags & FILL_MASK); }
    constexpr auto align() const noexcept -> Align { return Align((flags >> ALIGN_SHIFT) & 0b11u); }
    constexpr auto sign_plus() const noexcept -> bool { return bool(flags & SIGN_PLUS); }
    constexpr auto sign_minus() const noexcept -> bool { return bool(flags & SIGN_MINUS); }
    constexpr auto alternate() const noexcept -> bool { return bool(flags & ALTERNATE); }
    constexpr auto zero_pad() const noexcept -> bool { return bool(flags & ZERO_PAD); }
    constexpr auto is_debug() const noexcept -> bool { return bool(flags & DEBUG); }
    constexpr auto presentation() const noexcept -> Presentation {
        if (flags & DEBUG) return Presentation::Debug;
        if (flags & LOWER_EXP) return Presentation::LowerExp;
        if (flags & UPPER_EXP) return Presentation::UpperExp;
        return Presentation::Display;
    }
    constexpr auto has_width() const noexcept -> bool { return bool(flags & HAS_WIDTH); }
    constexpr auto has_prec() const noexcept -> bool { return bool(flags & HAS_PREC); }

    constexpr auto set_fill(char c) noexcept -> FormattingOptions& {
        flags = (flags & ~FILL_MASK) | u32(u8(c));
        return *this;
    }
    constexpr auto set_align(Align a) noexcept -> FormattingOptions& {
        flags = (flags & ~ALIGN_MASK) | (u32(a) << ALIGN_SHIFT);
        return *this;
    }
    constexpr auto set_width(u16 w) noexcept -> FormattingOptions& {
        width = w;
        flags |= HAS_WIDTH;
        return *this;
    }
    constexpr auto set_precision(u16 p) noexcept -> FormattingOptions& {
        precision = p;
        flags |= HAS_PREC;
        return *this;
    }
    constexpr auto set_flag(u32 f) noexcept -> FormattingOptions& {
        flags |= f;
        return *this;
    }
    constexpr auto set_presentation(Presentation value) noexcept -> FormattingOptions& {
        flags &= ~PRESENTATION_MASK;
        switch (value) {
        case Presentation::Display: break;
        case Presentation::Debug: flags |= DEBUG; break;
        case Presentation::LowerExp: flags |= LOWER_EXP; break;
        case Presentation::UpperExp: flags |= UPPER_EXP; break;
        }
        return *this;
    }
};

/// Trait for user-facing output formatting, invoked via `{}`.
///
/// Implementors provide `fmt(Formatter&)` to write a human-readable representation.
/// The Formatter's options() provides access to format specifiers (width, fill, align, etc.).
export struct Display {
    using Trait                  = Display;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = Display;
        auto fmt(Formatter& f) const -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fmt>;
};

/// Trait for programmer-facing debug output, invoked via `{:?}` or pretty-printed via `{:#?}`.
///
/// Implementors provide `fmt(Formatter&)` to write a debug representation.
/// Dispatched automatically by Argument when the format spec contains '?'.
export struct Debug {
    using Trait                  = Debug;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = Debug;
        auto fmt(Formatter& f) const -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fmt>;
};

/// Scientific notation with a lower-case `e`, invoked via `{:e}`.
export struct LowerExp {
    using Trait                  = LowerExp;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = LowerExp;
        auto fmt(Formatter& f) const -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fmt>;
};

/// Scientific notation with an upper-case `E`, invoked via `{:E}`.
export struct UpperExp {
    using Trait                  = UpperExp;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = UpperExp;
        auto fmt(Formatter& f) const -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fmt>;
};

/// Trait for a byte-oriented output sink used by the formatting machinery.
///
/// Implementors provide `write_str(const u8* p, usize len)` to accept raw bytes.
export struct Write {
    using Trait                  = Write;
    static constexpr bool direct = false;

    template<typename Self, typename = void>
    struct Api {
        using Trait = Write;
        auto write_str(const u8* p, usize len) -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write_str>;
};

/// The core formatting engine that drives output through a type-erased Write sink.
///
/// Holds a reference to the output writer and the current FormattingOptions.
/// Passed to Display::fmt and Debug::fmt to produce formatted output.
export struct Formatter {
private:
    void* _writer;
    auto (*_write_func)(void*, const u8*, usize) -> bool;
    FormattingOptions _options {};

public:
    template<typename W>
        requires Impled<W, Write>
    constexpr Formatter(W& writer) noexcept
        : _writer(rstd::addressof(writer)), _write_func([](void* w, const u8* p, usize len) {
              return as<Write>(*static_cast<W*>(w)).write_str(p, len);
          }) {}

    // Raw construction from type-erased writer (used by panic infrastructure).
    Formatter(void* writer, bool (*write_func)(void*, const u8*, usize)) noexcept
        : _writer(writer), _write_func(write_func) {}

    auto write_raw(const u8* p, usize len) -> bool { return _write_func(_writer, p, len); }
    auto write_fmt(struct Arguments args) -> bool;

    // Read-only access to current format options (valid inside Display::fmt / Debug::fmt).
    auto options() const noexcept -> FormattingOptions const& { return _options; }
    auto fill() const noexcept -> char { return _options.fill(); }
    auto align() const noexcept -> Align { return _options.align(); }
    auto sign_plus() const noexcept -> bool { return _options.sign_plus(); }
    auto sign_minus() const noexcept -> bool { return _options.sign_minus(); }
    auto alternate() const noexcept -> bool { return _options.alternate(); }
    auto zero_pad() const noexcept -> bool { return _options.zero_pad(); }
    auto is_debug() const noexcept -> bool { return _options.is_debug(); }
    auto has_width() const noexcept -> bool { return _options.has_width(); }
    auto has_prec() const noexcept -> bool { return _options.has_prec(); }
    auto width() const noexcept -> u16 { return _options.width; }
    auto precision() const noexcept -> u16 { return _options.precision; }

    auto pad_numeric(const u8* sign,
                     usize     sign_len,
                     const u8* significand,
                     usize     significand_len,
                     usize     zero_count,
                     const u8* exponent,
                     usize     exponent_len) -> bool;

    // write_fmt is the only one allowed to mutate _options.
    friend auto Formatter_set_options(Formatter& f, FormattingOptions opts) noexcept
        -> FormattingOptions {
        auto saved = f._options;
        f._options = opts;
        return saved;
    }
    friend auto Formatter_restore_options(Formatter& f, FormattingOptions opts) noexcept -> void {
        f._options = opts;
    }
};

/// A type-erased formatting argument that can dispatch to Display or Debug.
///
/// Created via `Argument::make(val)` and used internally by the format machinery.
export struct Argument {
private:
    const void* _ptr;
    auto (*_fmt_func)(const void*, Formatter&) -> bool;

public:
    // Construct from any type that implements Display and/or Debug.
    // Dispatches through the formatting trait selected by the placeholder.
    template<typename T>
        requires(Impled<T, Display> || Impled<T, Debug> || Impled<T, LowerExp> ||
                 Impled<T, UpperExp>)
    static auto make(const T& val) -> Argument {
        return { rstd::addressof(val), [](const void* p, Formatter& f) -> bool {
                    const T& self = *static_cast<const T*>(p);
                    switch (f.options().presentation()) {
                    case Presentation::Debug:
                        if constexpr (Impled<T, Debug>) {
                            return as<Debug>(self).fmt(f);
                        } else {
                            return false;
                        }
                    case Presentation::LowerExp:
                        if constexpr (Impled<T, LowerExp>) {
                            return as<LowerExp>(self).fmt(f);
                        } else {
                            return false;
                        }
                    case Presentation::UpperExp:
                        if constexpr (Impled<T, UpperExp>) {
                            return as<UpperExp>(self).fmt(f);
                        } else {
                            return false;
                        }
                    case Presentation::Display:
                        if constexpr (Impled<T, Display>) {
                            return as<Display>(self).fmt(f);
                        } else {
                            return false;
                        }
                    }
                } };
    }

    // Legacy alias kept for backward compatibility.
    template<typename T>
        requires Impled<T, Display>
    static auto display(const T& val) -> Argument {
        return make(val);
    }

    auto fmt(Formatter& f) const -> bool { return _fmt_func(_ptr, f); }

private:
    constexpr Argument(const void* p, auto (*func)(const void*, Formatter&)->bool)
        : _ptr(p), _fmt_func(func) {}
};

/// A pre-compiled set of format arguments: a format string plus its type-erased Argument array.
export struct Arguments {
    const u8*       fmt_ptr;
    usize           fmt_len;
    const Argument* args_ptr;
    usize           args_len;

    auto fmt(Formatter& f) const -> bool { return f.write_fmt(*this); }

    /// Build an `Arguments` view from a format string + variadic
    /// display-able args. Returns a small storage holder that owns the
    /// type-erased `Argument` array and converts implicitly to
    /// `Arguments`. Caller must keep the holder alive while the view
    /// is in use (same lifetime rule as `std::make_format_args`).
    template<typename... Args>
    static constexpr auto make(format_string<Args...> fmt_str, Args&&... args) noexcept
        -> detail::ArgumentsStorage<sizeof...(Args)>;
};

namespace detail
{
/// Storage holder for `Arguments::make`: owns the `Argument` array and
/// the format-string view; converts implicitly to a non-owning
/// `Arguments`. The N==0 branch sizes the array to 1 to keep the
/// trivial-copy POD valid for zero-arg formats.
template<usize N>
struct ArgumentsStorage {
    Argument  storage[N == 0 ? 1 : N];
    const u8* fmt_ptr;
    usize     fmt_len;

    constexpr operator Arguments() const noexcept { return { fmt_ptr, fmt_len, storage, N }; }
};
} // namespace detail

template<typename... Args>
constexpr auto Arguments::make(format_string<Args...> fmt_str, Args&&... args) noexcept
    -> detail::ArgumentsStorage<sizeof...(Args)> {
    return {
        { Argument::make(args)... },
        fmt_str.data(),
        fmt_str.size(),
    };
}

/// Checks whether a type can be formatted, i.e. it implements Display or Debug.
/// \tparam Tp The type to check.
export template<typename Tp, typename CharT = char>
concept formattable =
    Impled<Tp, Display> || Impled<Tp, Debug> || Impled<Tp, LowerExp> || Impled<Tp, UpperExp>;

// ── Compile-time format string validation ─────────────────────────────────
// These sentinel functions are non-constexpr on purpose: calling them inside
// a consteval context causes a hard compile error.
namespace fmt_check
{
[[noreturn]]
inline void unmatched_left_brace() {
    __builtin_unreachable();
}
[[noreturn]]
inline void unmatched_right_brace() {
    __builtin_unreachable();
}
[[noreturn]]
inline void too_few_args() {
    __builtin_unreachable();
}

consteval void check(const char* s, usize n, usize n_args) {
    usize count = 0;
    for (usize i = 0; i < n; ++i) {
        if (s[i] == '{') {
            if (i + 1 < n && s[i + 1] == '{') {
                ++i; // skip {{
            } else {
                ++i;
                while (i < n && s[i] != '}') ++i;
                if (i >= n) unmatched_left_brace();
                ++count;
            }
        } else if (s[i] == '}') {
            if (i + 1 < n && s[i + 1] == '}') {
                ++i; // skip }}
            } else {
                unmatched_right_brace();
            }
        }
    }
    if (count > n_args) too_few_args();
}
} // namespace fmt_check

/// A compile-time validated format string that ensures argument count and brace matching.
/// \tparam Args The types of the format arguments.
template<typename... Args>
struct FormatString {
    const char* _ptr;
    usize       _len;

    template<usize N>
    consteval FormatString(const char (&s)[N]) noexcept: _ptr(s), _len(N - 1) {
        fmt_check::check(s, N - 1, sizeof...(Args));
    }

    auto data() const noexcept -> const u8* { return reinterpret_cast<const u8*>(_ptr); }
    auto size() const noexcept -> usize { return _len; }
};

// `format_string` alias declared at the top of this namespace alongside
// other forward declarations.

} // namespace rstd::fmt

namespace rstd
{

// Arguments implements Display: writing it re-runs the format substitution.
// Enables format("{}", some_arguments) and format("{}", panic_info.message).
template<>
struct Impl<fmt::Display, fmt::Arguments> : ImplBase<fmt::Arguments> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.write_fmt(this->self()); }
};

} // namespace rstd
