export module rstd.core:fmt.debug;
export import :fmt;
import :str.str;
import :option;
using namespace rstd::literals;

namespace rstd::fmt
{
struct DebugIndented {
    Formatter&  target;
    bool        pretty;
    bool        line_start = false;
    static auto write(void* pointer, const uint8_t* bytes, size_t length) -> bool {
        // The nested formatter supplies its paired state and a readable byte range.
        [[unsafe]] {
            auto& state = *static_cast<DebugIndented*>(pointer);
            for (size_t offset = 0; offset < length;) {
                if (state.pretty && state.line_start && ! state.target.write_str("    "_str))
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
    }
};
} // namespace rstd::fmt

export namespace rstd::fmt
{
class DebugStruct {
    Formatter& formatter_;
    bool       good_;
    bool       first_    = true;
    bool       finished_ = false;
    bool       pretty_;

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
        DebugIndented state { formatter_, pretty_ };
        Formatter     nested(&state, DebugIndented::write);
        auto          options = formatter_.options();
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

class DebugTuple {
    Formatter& formatter_;
    bool       good_;
    bool       first_    = true;
    bool       finished_ = false;
    bool       pretty_;
    bool       unnamed_;
    size_t     fields_ {};

public:
    DebugTuple(Formatter& formatter, ref<str> name)
        : formatter_(formatter),
          good_(formatter.write_str(name)),
          pretty_(formatter.alternate()),
          unnamed_(name.is_empty()) {}

    template<typename T>
    auto field(const T& value) -> DebugTuple& {
        static_assert(Impled<T, Debug>, "Debug fields must implement rstd::fmt::Debug");
        if (finished_) good_ = false;
        if (! good_) return *this;
        if (first_) good_ = formatter_.write_str("("_str);
        good_  = good_ && formatter_.write_str(pretty_ ? (first_ ? "\n    "_str : ",\n    "_str)
                                                       : (first_ ? ""_str : ", "_str));
        first_ = false;
        ++fields_;
        if (! good_) return *this;
        DebugIndented state { formatter_, pretty_ };
        Formatter     nested(&state, DebugIndented::write);
        auto          options = formatter_.options();
        options.set_presentation(Presentation::Debug);
        options.flags &= ~(FormattingOptions::HAS_WIDTH | FormattingOptions::HAS_PREC);
        (void)Formatter_set_options(nested, options);
        if constexpr (Impled<T, Debug>) good_ = as<Debug>(value).fmt(nested);
        return *this;
    }
    auto finish() -> bool {
        if (finished_) return false;
        finished_ = true;
        if (first_) return good_ && (! unnamed_ || formatter_.write_str("()"_str));
        return good_ && formatter_.write_str(pretty_                    ? ",\n)"_str
                                             : unnamed_ && fields_ == 1 ? ",)"_str
                                                                        : ")"_str);
    }
};
class DebugList {
    Formatter& formatter_;
    bool       good_;
    bool       first_    = true;
    bool       finished_ = false;
    bool       pretty_;

public:
    explicit DebugList(Formatter& formatter)
        : formatter_(formatter),
          good_(formatter.write_str("["_str)),
          pretty_(formatter.alternate()) {}
    template<typename T>
    auto entry(const T& value) -> DebugList& {
        static_assert(Impled<T, Debug>, "Debug list entries must implement rstd::fmt::Debug");
        if (finished_) good_ = false;
        if (! good_) return *this;
        good_  = formatter_.write_str(pretty_ ? (first_ ? "\n    "_str : ",\n    "_str)
                                              : (first_ ? ""_str : ", "_str));
        first_ = false;
        if (! good_) return *this;
        DebugIndented state { formatter_, pretty_ };
        Formatter     nested(&state, DebugIndented::write);
        auto          options = formatter_.options();
        options.set_presentation(Presentation::Debug);
        options.flags &= ~(FormattingOptions::HAS_WIDTH | FormattingOptions::HAS_PREC);
        (void)Formatter_set_options(nested, options);
        if constexpr (Impled<T, Debug>) good_ = as<Debug>(value).fmt(nested);
        return *this;
    }
    auto finish() -> bool {
        if (finished_) return false;
        finished_ = true;
        return good_ && formatter_.write_str(first_ ? "]"_str : pretty_ ? ",\n]"_str : "]"_str);
    }
};

class DebugMap {
    Formatter& formatter_;
    bool       good_;
    bool       first_    = true;
    bool       finished_ = false;
    bool       pretty_;

    template<typename T>
    auto value(const T& field) -> bool {
        static_assert(Impled<T, Debug>,
                      "Debug map keys and values must implement rstd::fmt::Debug");
        DebugIndented state { formatter_, pretty_ };
        Formatter     nested(&state, DebugIndented::write);
        auto          options = formatter_.options();
        options.set_presentation(Presentation::Debug);
        options.flags &= ~(FormattingOptions::HAS_WIDTH | FormattingOptions::HAS_PREC);
        (void)Formatter_set_options(nested, options);
        if constexpr (Impled<T, Debug>)
            return as<Debug>(field).fmt(nested);
        else
            return false;
    }

public:
    explicit DebugMap(Formatter& formatter)
        : formatter_(formatter),
          good_(formatter.write_str("{"_str)),
          pretty_(formatter.alternate()) {}

    template<typename K, typename V>
    auto entry(const K& key, const V& field) -> DebugMap& {
        if (finished_) good_ = false;
        if (! good_) return *this;
        good_  = formatter_.write_str(pretty_ ? (first_ ? "\n    "_str : ",\n    "_str)
                                              : (first_ ? ""_str : ", "_str)) &&
                 value(key) && formatter_.write_str(": "_str) && value(field);
        first_ = false;
        return *this;
    }
    auto finish() -> bool {
        if (finished_) return false;
        finished_ = true;
        return good_ && formatter_.write_str(first_ ? "}"_str : pretty_ ? ",\n}"_str : "}"_str);
    }
};
} // namespace rstd::fmt

export namespace rstd
{
template<typename... Ts>
    requires(Impled<mtp::rm_cvf<Ts>, fmt::Debug> && ...)
struct Impl<fmt::Debug, tuple<Ts...>> : ImplBase<tuple<Ts...>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        fmt::DebugTuple output(formatter, ""_str);
        rstd::apply(
            [&](const auto&... values) {
                (output.field(values), ...);
            },
            this->self());
        return output.finish();
    }
};

template<typename T>
    requires Impled<T, fmt::Debug>
struct Impl<fmt::Debug, ref<T[]>> : ImplBase<ref<T[]>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        fmt::DebugList output(formatter);
        for (const auto& value : this->self()) output.entry(value);
        return output.finish();
    }
};
template<typename T>
    requires Impled<T, fmt::Debug>
struct Impl<fmt::Debug, mut_ref<T[]>> : ImplBase<mut_ref<T[]>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto values = this->self().deref();
        return as<fmt::Debug>(values).fmt(formatter);
    }
};

template<typename T>
    requires Impled<mtp::rm_cvf<T>, fmt::Debug>
struct Impl<fmt::Debug, Option<T>> : ImplBase<Option<T>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& value = this->self();
        if (value.is_none()) return formatter.write_str("None"_str);
        fmt::DebugTuple output(formatter, "Some"_str);
        return output.field(*value).finish();
    }
};
} // namespace rstd
