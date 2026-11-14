import rstd.serde;
import rstd.json;
struct [[pmacro::derive("serde::derive"), pmacro::helper("serde::derive", described_payload)]]
Payload {
    [[pmacro::helper("serde::derive", rename("wire"))]]
    rstd::u64             value;
    friend constexpr auto rstd_payload_definition(Payload*) {
        return rstd::VariantPayload<Payload,
                                    rstd::VariantKind::Tuple,
                                    "Payload",
                                    rstd::PayloadField<"value", &Payload::value>> {};
    }
};
auto use() {
    return rstd::json::encode(Payload {});
}
