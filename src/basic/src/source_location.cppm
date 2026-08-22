module;

#if ! defined(__clang__)
#error "rstd.basic:source_location requires Clang"
#endif

export module rstd.basic:source_location;
import :std;

static_assert(__has_builtin(__builtin_FILE), "rstd.basic:source_location requires __builtin_FILE");
static_assert(__has_builtin(__builtin_FUNCTION),
              "rstd.basic:source_location requires __builtin_FUNCTION");
static_assert(__has_builtin(__builtin_LINE), "rstd.basic:source_location requires __builtin_LINE");
static_assert(__has_builtin(__builtin_COLUMN),
              "rstd.basic:source_location requires __builtin_COLUMN");

export namespace rstd
{

class source_location {
    const char*    file_ { "" };
    const char*    function_ { "" };
    uint_least32_t line_ {};
    uint_least32_t column_ {};

    constexpr source_location(const char* file,
                              const char* function,
                              unsigned    line,
                              unsigned    column) noexcept
        : file_(file), function_(function), line_(line), column_(column) {}

public:
    constexpr source_location() noexcept = default;

    static consteval auto current(const char* file     = __builtin_FILE(),
                                  const char* function = __builtin_FUNCTION(),
                                  unsigned    line     = __builtin_LINE(),
                                  unsigned    column   = __builtin_COLUMN()) noexcept
        -> source_location {
        return source_location { file, function, line, column };
    }

    constexpr auto file_name() const noexcept -> const char* { return file_; }
    constexpr auto function_name() const noexcept -> const char* { return function_; }
    constexpr auto line() const noexcept -> uint_least32_t { return line_; }
    constexpr auto column() const noexcept -> uint_least32_t { return column_; }
};

} // namespace rstd
