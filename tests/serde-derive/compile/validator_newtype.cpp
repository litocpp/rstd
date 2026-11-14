import rstd.serde;
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", newtype, validate([](const auto&) {
                            return true;
                        }))]] Invalid {
    rstd::i64 value;
};
