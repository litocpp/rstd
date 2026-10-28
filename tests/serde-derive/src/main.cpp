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
    return 0;
}
