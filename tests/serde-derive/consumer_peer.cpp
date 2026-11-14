import rstd.tests.serde_derive;
import rstd.json;

namespace serde_derive::nested
{
auto decode_in_peer(rstd::ref<rstd::str> input) -> rstd::Result<Adjacent, rstd::json::DecodeError> {
    return rstd::json::decode<Adjacent>(input);
}
auto decode_factory_in_peer(rstd::ref<rstd::str> input)
    -> rstd::Result<FactoryRecord, rstd::json::DecodeError> {
    return rstd::json::decode<FactoryRecord>(input);
}
} // namespace serde_derive::nested
