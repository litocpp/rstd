export module rstd.tests.derive.fixture;

import rstd.core;
import rstd.alloc;

namespace derive_fixture
{

export class [[pmacro::derive("rstd::Clone", "rstd::Debug", "rstd::Display", "rstd::FromStr")]]
[[pmacro::helper("rstd::Display", transparent)]] [[pmacro::helper("rstd::FromStr", transparent)]]
ExportedNumber {
public:
    rstd::i32 value;
};

export class [[pmacro::derive("rstd::Clone")]] ExportedPrivateClone {
    friend constexpr auto rstd_clone_definition(ExportedPrivateClone*);
    alloc::string::String text_;

    explicit ExportedPrivateClone(alloc::string::String text): text_(rstd::move(text)) {}

public:
    static auto make(rstd::ref<rstd::str> text) -> ExportedPrivateClone {
        return ExportedPrivateClone(alloc::string::String::make(text));
    }
    auto text() const -> rstd::ref<rstd::str> { return text_.as_str(); }
};

namespace nested
{

export {
    class [[pmacro::derive("rstd::Clone")]] ExportedClone {
        friend constexpr auto rstd_clone_definition(ExportedClone*);
        alloc::string::String text_;

        explicit ExportedClone(alloc::string::String text): text_(rstd::move(text)) {}

    public:
        static auto make(rstd::ref<rstd::str> text) -> ExportedClone {
            return ExportedClone(alloc::string::String::make(text));
        }
        auto text() const -> rstd::ref<rstd::str> { return text_.as_str(); }
    };

    enum class [[pmacro::derive(
        "rstd::Debug",
        "rstd::Display",
        "rstd::FromStr",
        "rstd::EnumIter",
        "rstd::TryFrom")]] [[pmacro::helper("rstd::FromStr", ascii_case_insensitive)]] ExportedState
    {
        Idle,
        Ready [[pmacro::helper("rstd::Display", rename("ready"))]] [[pmacro::helper("rstd::FromStr",
                                                                                    rename("ready"),
                                                                                    alias("ok"))]],
    };

    struct [[pmacro::derive("rstd::Debug", "rstd::Display")]] [[pmacro::helper(
        "rstd::Display",
        format("{name}: {count}"))]] ExportedSummary {
        rstd::i32            count;
        rstd::ref<rstd::str> name;
    };

} // export

} // namespace nested
} // namespace derive_fixture
