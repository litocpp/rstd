export module rstd.core:fmt.debug;
export import :fmt;
import :str.str;
using namespace rstd::literals;

export namespace rstd::fmt
{
class DebugStruct {
    Formatter& formatter_;
    bool       good_;
    bool       first_    = true;
    bool       finished_ = false;
    bool       pretty_;

    struct Indented {
        Formatter& target;
        bool       enabled;
        bool       line_start = false;

        static auto write(void* pointer, const uint8_t* bytes, size_t length) -> bool {
            auto&  state  = *static_cast<Indented*>(pointer);
            size_t offset = 0;
            while (offset < length) {
                if (state.enabled && state.line_start && ! state.target.write_str("    "_str))
                    return false;
                auto end = offset;
                while (end < length && bytes[end] != '\n') ++end;
                state.line_start = end < length;
                if (state.line_start) ++end;
                if (! state.target.write_raw(bytes + offset, end - offset)) return false;
                offset = end;
            }
            return true;
        }
    };

public:
    DebugStruct(Formatter& formatter, ref<str> name)
        : formatter_(formatter),
          good_(formatter.write_str(name) && formatter.write_str(" {"_str)),
          pretty_(formatter.alternate()) {}

    template<typename T>
    auto field(ref<str> name, const T& value) -> DebugStruct& {
        static_assert(Impled<T, Debug>, "Debug fields must implement rstd::fmt::Debug");
        if (finished_) good_ = false;
        if (! good_) return *this;
        good_  = formatter_.write_str(pretty_ ? (first_ ? "\n    "_str : ",\n    "_str)
                                              : (first_ ? " "_str : ", "_str)) &&
                 formatter_.write_str(name) && formatter_.write_str(": "_str);
        first_ = false;
        if (! good_) return *this;
        Indented  state { formatter_, pretty_ };
        Formatter nested(&state, Indented::write);
        auto      options = formatter_.options();
        options.set_presentation(Presentation::Debug);
        options.flags &= ~(FormattingOptions::HAS_WIDTH | FormattingOptions::HAS_PREC);
        (void)Formatter_set_options(nested, options);
        if constexpr (Impled<T, Debug>) good_ = as<Debug>(value).fmt(nested);
        return *this;
    }

    auto finish() -> bool {
        if (finished_) return false;
        finished_ = true;
        if (! good_) return false;
        return formatter_.write_str(first_ ? "}"_str : pretty_ ? ",\n}"_str : " }"_str);
    }
};
} // namespace rstd::fmt
