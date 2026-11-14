import rstd.json;
using namespace rstd::literals;
struct [[pmacro::derive("serde::derive")]] Invalid {
    [[pmacro::helper("serde::derive", default([](int value) {
                         return rstd::i64(value);
                     }))]]
    rstd::i64 value;
};
void decode_invalid() {
    (void)rstd::json::decode<Invalid>("{}"_str);
}
