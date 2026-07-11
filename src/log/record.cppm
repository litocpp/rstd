export module rstd.log:record;
export import :level;
export import rstd.core;

namespace rstd::log
{

/// A target tag used to specify the logging target in macro-like calls.
///
/// Default-constructs to an empty target; wraps a `ref<str>`.
export struct Target {
    ref<str> value;
    constexpr Target(ref<str> s = {}) noexcept: value(s) {}
};

/// Metadata about a log message, carrying level and target.
export struct Metadata {
    Level    level;
    ref<str> target;

    Metadata() = default;
    constexpr Metadata(Level l, ref<str> t) noexcept: level(l), target(t) {}

    auto lvl() const noexcept -> Level { return level; }
    auto tgt() const noexcept -> ref<str> { return target; }
};

/// A builder for Metadata.
export struct MetadataBuilder {
    Level    level { Level::Info };
    ref<str> target;

    auto set_level(Level l) noexcept -> MetadataBuilder& {
        level = l;
        return *this;
    }
    auto set_target(ref<str> t) noexcept -> MetadataBuilder& {
        target = t;
        return *this;
    }
    auto build() const noexcept -> Metadata { return { level, target }; }
};

/// The payload of a log message, passed to Log::log().
///
/// Mirrors Rust's log::Record: metadata + formatted args + source location.
export struct Record {
    Metadata         metadata;
    fmt::Arguments   args;
    panic_::Location location;

    Record() = default;
    constexpr Record(Metadata m, fmt::Arguments a, panic_::Location loc = {}) noexcept
        : metadata(m), args(a), location(loc) {}

    auto lvl() const noexcept -> Level { return metadata.level; }
    auto target() const noexcept -> ref<str> { return metadata.target; }
    auto args_() const noexcept -> fmt::Arguments { return args; }
    auto loc() const noexcept -> panic_::Location { return location; }
    auto file() const noexcept -> const char* { return location.file_name(); }
    auto line_no() const noexcept -> u32 { return location.line(); }
};

/// A builder for Record.
export struct RecordBuilder {
    Metadata         metadata { MetadataBuilder().build() };
    fmt::Arguments   args { nullptr, 0, nullptr, 0 };
    panic_::Location location {};

    auto set_metadata(Metadata m) noexcept -> RecordBuilder& {
        metadata = m;
        return *this;
    }
    auto set_level(Level l) noexcept -> RecordBuilder& {
        metadata.level = l;
        return *this;
    }
    auto set_target(ref<str> t) noexcept -> RecordBuilder& {
        metadata.target = t;
        return *this;
    }
    auto set_args(fmt::Arguments a) noexcept -> RecordBuilder& {
        args = a;
        return *this;
    }
    auto set_location(panic_::Location loc) noexcept -> RecordBuilder& {
        location = loc;
        return *this;
    }
    auto build() const noexcept -> Record { return { metadata, args, location }; }
};

} // namespace rstd::log
