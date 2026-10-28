import rstd;
import rstd.serde;
import rstd.json;
import rstd.toml;
import serde.sample;
using namespace rstd::prelude;
using namespace rstd::literals;
int main() {
    static_assert(rstd::serde::Serializable<sample::Config>);
    static_assert(rstd::serde::Deserializable<sample::Config>);
    static_assert(! rstd::serde::Serializable<sample::InputOnly>);
    static_assert(rstd::serde::Deserializable<sample::InputOnly>);
    static_assert(rstd::serde::Serializable<sample::OutputOnly>);
    static_assert(! rstd::serde::Deserializable<sample::OutputOnly>);
    auto decoded = rstd::json::decode<sample::Config>(
        R"({"name":"sample","mode":"Fast","values":[1,2],"note":"x"})"_str);
    if (decoded.is_err()) return 1;
    if (decoded->count != u32(7) || decoded->hidden != u32(9)) return 2;
    auto json = rstd::json::encode(*decoded);
    if (json.is_err()) return 3;
    auto toml = rstd::toml::encode(*decoded);
    if (toml.is_err()) return 4;
    auto again = rstd::toml::decode<sample::Config>(toml->as_str());
    if (again.is_err() || again->name != decoded->name) return 5;
    if (rstd::json::decode<sample::Config>(
            R"({"label":"x","name":"y","mode":"Fast","values":[],"note":null})"_str)
            .is_ok())
        return 6;
    auto domain = rstd::json::decode<sample::DomainEnvelope>(
        R"({"result":{"Err":{"Fields":{"message":"broken","code":7}}}})"_str);
    if (domain.is_err() || domain->result.is_ok() ||
        ! domain->result.unwrap_err_unchecked().is_Fields())
        return 7;
    auto domain_toml = rstd::toml::encode(*domain);
    if (domain_toml.is_err()) return 8;
    auto domain_again = rstd::toml::decode<sample::DomainEnvelope>(domain_toml->as_str());
    if (domain_again.is_err() ||
        domain_again->result.unwrap_err_unchecked().as_Fields().code != u32(7))
        return 9;
    auto legacy = rstd::json::decode<sample::LegacyError>(R"({"Code":{"value":17}})"_str);
    if (legacy.is_err() || legacy->as_Code().value != u32(17)) return 10;
    auto scalar = rstd::json::decode<sample::DomainError>(R"({"Code":23})"_str);
    if (scalar.is_err() || scalar->as_Code() != u32(23)) return 11;
    auto unit = rstd::json::decode<sample::DomainError>(R"("Missing")"_str);
    if (unit.is_err() || ! unit->is_Missing()) return 12;
    auto option =
        rstd::json::decode<rstd::serde::TaggedOption<Option<u32>>>(R"({"Some":null})"_str);
    if (option.is_err() || option->value.is_none() || option->value->is_some()) return 13;
    return 0;
}
