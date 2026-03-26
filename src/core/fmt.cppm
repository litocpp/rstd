export module rstd.core:fmt;
export import :trait;
export import rstd.basic;

namespace rstd::fmt

{

export struct Formatter;

// Display Trait
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

// Debug Trait
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

// Write Trait
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

export struct Formatter {
private:
    void* _writer;
    auto (*_write_func)(void*, const u8*, usize) -> bool;

public:
    template<typename W>
        requires Impled<W, Write>
    constexpr Formatter(W& writer) noexcept
        : _writer(rstd::addressof(writer)), _write_func([](void* w, const u8* p, usize len) {
              return as<Write>(*static_cast<W*>(w)).write_str(p, len);
          }) {}

    auto write_raw(const u8* p, usize len) -> bool { return _write_func(_writer, p, len); }

    auto write_fmt(struct Arguments args) -> bool;
};

// Type-erased argument for formatting
export struct Argument {
private:
    const void* _ptr;
    auto (*_fmt_func)(const void*, Formatter&) -> bool;

public:
    template<typename T>
        requires Impled<T, Display>
    static auto display(const T& val) -> Argument {
        return { rstd::addressof(val), [](const void* p, Formatter& f) {
                    return as<Display>(*static_cast<const T*>(p)).fmt(f);
                } };
    }

    template<typename T>
        requires Impled<T, Debug>
    static auto debug(const T& val) -> Argument {
        return { rstd::addressof(val), [](const void* p, Formatter& f) {
                    return as<Debug>(*static_cast<const T*>(p)).fmt(f);
                } };
    }

    auto fmt(Formatter& f) const -> bool { return _fmt_func(_ptr, f); }

private:
    constexpr Argument(const void* p, auto (*func)(const void*, Formatter&)->bool)
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

// Compile-time format string validation
// These are non-constexpr: calling them inside a consteval function causes a compile error.
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
                ++i; // skip escaped {{
            } else {
                ++i;
                while (i < n && s[i] != '}') ++i;
                if (i >= n) unmatched_left_brace();
                ++count;
            }
        } else if (s[i] == '}') {
            if (i + 1 < n && s[i + 1] == '}') {
                ++i; // skip escaped }}
            } else {
                unmatched_right_brace();
            }
        }
    }
    if (count > n_args) too_few_args();
}
} // namespace fmt_check

// Non-deduced identity wrapper (not exported).
// typename _FmtId<T>::type is a non-deduced context, preventing Args from
// being deduced from the FormatString parameter — deduction happens only
// from the trailing Args&&... parameter instead.
template<typename T>
struct _FmtId { using type = T; };

// A compile-time checked format string.
// The consteval constructor validates placeholder count against sizeof...(Args).
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

// Type alias that puts Args in a non-deduced context.
// format_string<A, B> == FormatString<A, B>, but Args cannot be deduced
// from this alias in a function parameter — only from the trailing args.
export template<typename... Args>
using format_string = FormatString<typename _FmtId<Args>::type...>;

} // namespace rstd::fmt
