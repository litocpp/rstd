export module rstd.core:fmt;
export import :trait;
export import rstd.basic;

namespace rstd::fmt

{

export struct Formatter;

// ── Alignment ─────────────────────────────────────────────────────────────
export enum class Align : u32 { None = 0, Left = 1, Right = 2, Center = 3 };

// ── FormattingOptions ──────────────────────────────────────────────────────
// Bit layout (mirrors Rust's FormattingOptions):
//   [20: 0]  fill character (Unicode scalar, default U+0020 ' ')
//   [22:21]  alignment  0=none 1=left 2=right 3=center
//   [23]     sign_plus   (+)
//   [24]     sign_minus  (-)
//   [25]     alternate   (#)
//   [26]     zero_pad    (0)
//   [27]     debug       (?)
//   [28]     has_width
//   [29]     has_precision
export struct FormattingOptions {
    u32 flags     = u32(' '); // fill=' ', all flags clear
    u16 width     = 0;
    u16 precision = 0;

    static constexpr u32 FILL_MASK   = 0x1F'FFFFu;
    static constexpr u32 ALIGN_SHIFT = 21u;
    static constexpr u32 ALIGN_MASK  = 0b11u << 21u;
    static constexpr u32 SIGN_PLUS   = 1u << 23u;
    static constexpr u32 SIGN_MINUS  = 1u << 24u;
    static constexpr u32 ALTERNATE   = 1u << 25u;
    static constexpr u32 ZERO_PAD    = 1u << 26u;
    static constexpr u32 DEBUG       = 1u << 27u;
    static constexpr u32 HAS_WIDTH   = 1u << 28u;
    static constexpr u32 HAS_PREC    = 1u << 29u;

    constexpr auto fill()      const noexcept -> char  { return char(flags & FILL_MASK); }
    constexpr auto align()     const noexcept -> Align { return Align((flags >> ALIGN_SHIFT) & 0b11u); }
    constexpr auto sign_plus() const noexcept -> bool  { return bool(flags & SIGN_PLUS); }
    constexpr auto sign_minus()const noexcept -> bool  { return bool(flags & SIGN_MINUS); }
    constexpr auto alternate() const noexcept -> bool  { return bool(flags & ALTERNATE); }
    constexpr auto zero_pad()  const noexcept -> bool  { return bool(flags & ZERO_PAD); }
    constexpr auto is_debug()  const noexcept -> bool  { return bool(flags & DEBUG); }
    constexpr auto has_width() const noexcept -> bool  { return bool(flags & HAS_WIDTH); }
    constexpr auto has_prec()  const noexcept -> bool  { return bool(flags & HAS_PREC); }

    constexpr auto set_fill(char c) noexcept -> FormattingOptions& {
        flags = (flags & ~FILL_MASK) | u32(u8(c));
        return *this;
    }
    constexpr auto set_align(Align a) noexcept -> FormattingOptions& {
        flags = (flags & ~ALIGN_MASK) | (u32(a) << ALIGN_SHIFT);
        return *this;
    }
    constexpr auto set_width(u16 w) noexcept -> FormattingOptions& {
        width = w; flags |= HAS_WIDTH; return *this;
    }
    constexpr auto set_precision(u16 p) noexcept -> FormattingOptions& {
        precision = p; flags |= HAS_PREC; return *this;
    }
    constexpr auto set_flag(u32 f) noexcept -> FormattingOptions& {
        flags |= f; return *this;
    }
};

// ── Display Trait ──────────────────────────────────────────────────────────
// User-facing output: {}
// fmt(f) reads f.options() for spec (width, fill, align, sign, alternate, ...).
export struct Display {
    using Trait                  = Display;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api : ImplBase<Delegate> {
        using Trait = Display;
        auto fmt(Formatter& f) const -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fmt>;
};

// ── Debug Trait ────────────────────────────────────────────────────────────
// Programmer-facing output: {:?}  pretty: {:#?}
// Dispatched automatically by Argument when the format spec contains '?'.
export struct Debug {
    using Trait                  = Debug;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api : ImplBase<Delegate> {
        using Trait = Debug;
        auto fmt(Formatter& f) const -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::fmt>;
};

// ── Write Trait ────────────────────────────────────────────────────────────
export struct Write {
    using Trait                  = Write;
    static constexpr bool direct = false;

    template<typename Self, typename Delegate = void>
    struct Api : ImplBase<Delegate> {
        using Trait = Write;
        auto write_str(const u8* p, usize len) -> bool;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::write_str>;
};

// ── Formatter ─────────────────────────────────────────────────────────────
export struct Formatter {
private:
    void*  _writer;
    auto (*_write_func)(void*, const u8*, usize) -> bool;
    FormattingOptions _options{};

public:
    template<typename W>
        requires Impled<W, Write>
    constexpr Formatter(W& writer) noexcept
        : _writer(rstd::addressof(writer)),
          _write_func([](void* w, const u8* p, usize len) {
              return as<Write>(*static_cast<W*>(w)).write_str(p, len);
          }) {}

    auto write_raw(const u8* p, usize len) -> bool { return _write_func(_writer, p, len); }
    auto write_fmt(struct Arguments args) -> bool;

    // Read-only access to current format options (valid inside Display::fmt / Debug::fmt).
    auto options()     const noexcept -> FormattingOptions const& { return _options; }
    auto fill()        const noexcept -> char  { return _options.fill(); }
    auto align()       const noexcept -> Align { return _options.align(); }
    auto sign_plus()   const noexcept -> bool  { return _options.sign_plus(); }
    auto sign_minus()  const noexcept -> bool  { return _options.sign_minus(); }
    auto alternate()   const noexcept -> bool  { return _options.alternate(); }
    auto zero_pad()    const noexcept -> bool  { return _options.zero_pad(); }
    auto is_debug()    const noexcept -> bool  { return _options.is_debug(); }
    auto has_width()   const noexcept -> bool  { return _options.has_width(); }
    auto has_prec()    const noexcept -> bool  { return _options.has_prec(); }
    auto width()       const noexcept -> u16   { return _options.width; }
    auto precision()   const noexcept -> u16   { return _options.precision; }

    // write_fmt is the only one allowed to mutate _options.
    friend auto Formatter_set_options(Formatter& f, FormattingOptions opts) noexcept -> FormattingOptions {
        auto saved  = f._options;
        f._options  = opts;
        return saved;
    }
    friend auto Formatter_restore_options(Formatter& f, FormattingOptions opts) noexcept -> void {
        f._options = opts;
    }
};

// ── Argument ──────────────────────────────────────────────────────────────
export struct Argument {
private:
    const void* _ptr;
    auto (*_fmt_func)(const void*, Formatter&) -> bool;

public:
    // Construct from any type that implements Display and/or Debug.
    // Spec dispatch: if Formatter::is_debug() → Debug::fmt, else → Display::fmt.
    template<typename T>
        requires (Impled<T, Display> || Impled<T, Debug>)
    static auto make(const T& val) -> Argument {
        return { rstd::addressof(val),
                 [](const void* p, Formatter& f) -> bool {
                     const T& self = *static_cast<const T*>(p);
                     if (f.is_debug()) {
                         if constexpr (Impled<T, Debug>) {
                             return as<Debug>(self).fmt(f);
                         } else {
                             return false; // T not Debug-able
                         }
                     } else {
                         if constexpr (Impled<T, Display>) {
                             return as<Display>(self).fmt(f);
                         } else {
                             return false; // T only Debug-able
                         }
                     }
                 } };
    }

    // Legacy alias kept for backward compatibility.
    template<typename T>
        requires Impled<T, Display>
    static auto display(const T& val) -> Argument { return make(val); }

    auto fmt(Formatter& f) const -> bool { return _fmt_func(_ptr, f); }

private:
    constexpr Argument(const void* p, auto (*func)(const void*, Formatter&) -> bool)
        : _ptr(p), _fmt_func(func) {}
};

export struct Arguments {
    const u8*       fmt_ptr;
    usize           fmt_len;
    const Argument* args_ptr;
    usize           args_len;

    auto fmt(Formatter& f) const -> bool { return f.write_fmt(*this); }
};

export template<typename Tp, typename CharT = char>
concept formattable = Impled<Tp, Display> || Impled<Tp, Debug>;

// ── Compile-time format string validation ─────────────────────────────────
// These sentinel functions are non-constexpr on purpose: calling them inside
// a consteval context causes a hard compile error.
namespace fmt_check
{
[[noreturn]] inline void unmatched_left_brace()  { __builtin_unreachable(); }
[[noreturn]] inline void unmatched_right_brace() { __builtin_unreachable(); }
[[noreturn]] inline void too_few_args()          { __builtin_unreachable(); }

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

template<typename T>
struct _FmtId { using type = T; };

export template<typename... Args>
struct FormatString {
    const char* _ptr;
    usize       _len;

    template<usize N>
    consteval FormatString(const char (&s)[N]) noexcept
        : _ptr(s), _len(N - 1) {
        fmt_check::check(s, N - 1, sizeof...(Args));
    }

    auto data() const noexcept -> const u8* { return reinterpret_cast<const u8*>(_ptr); }
    auto size() const noexcept -> usize { return _len; }
};

export template<typename... Args>
using format_string = FormatString<typename _FmtId<Args>::type...>;

} // namespace rstd::fmt
