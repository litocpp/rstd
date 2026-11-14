import rstd.serde;
template<typename T>
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", described_payload)]]
Payload {
    T value;
};
