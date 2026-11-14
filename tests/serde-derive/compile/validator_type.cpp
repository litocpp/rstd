import rstd.serde;
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", validate([](const auto&) {
                                                             return true;
                                                         }))]] Invalid {
    rstd::i64 value;
};
