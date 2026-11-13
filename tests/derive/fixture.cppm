export module rstd.tests.derive.fixture;

import rstd.core;

namespace derive_fixture
{

export class [[pmacro::derive("rstd::Debug", "rstd::Display", "rstd::FromStr")]] [[pmacro::helper(
    "rstd::Display",
    transparent)]] [[pmacro::helper("rstd::FromStr", transparent)]] ExportedNumber {
public:
    rstd::i32 value;
};

namespace nested
{

export {
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
