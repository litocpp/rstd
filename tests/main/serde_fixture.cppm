export module rstd.tests.serde_fixture;
import rstd.serde;

using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace rstd::tests
{

struct WireConfig {
    String         name;
    Vec<rstd::u64> ports;
};

} // namespace rstd::tests

export template<>
struct rstd::Impl<rstd::serde::Serialize, rstd::tests::WireConfig> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const rstd::tests::WireConfig& value) ->
        typename Serializer::result_type {
        auto map = serializer.begin_map(rstd::usize(2));
        if (map.is_err()) return rstd::Err(rstd::move(map).unwrap_err_unchecked());
        auto output = rstd::move(map).unwrap_unchecked();
        auto name   = rstd::serde::field(output, "name"_str, value.name);
        if (name.is_err()) return rstd::Err(rstd::move(name).unwrap_err_unchecked());
        auto ports = rstd::serde::field(output, "ports"_str, value.ports);
        if (ports.is_err()) return rstd::Err(rstd::move(ports).unwrap_err_unchecked());
        return output.end();
    }
};

export template<>
struct rstd::Impl<rstd::serde::Deserialize, rstd::tests::WireConfig> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<rstd::tests::WireConfig, typename Deserializer::error_type> {
        auto map = deserializer.begin_map();
        if (map.is_err()) return rstd::Err(rstd::move(map).unwrap_err_unchecked());
        auto input = rstd::move(map).unwrap_unchecked();
        auto name  = rstd::Option<String>();
        auto ports = rstd::Option<Vec<rstd::u64>>();
        for (;;) {
            auto key = input.template next_key<String>();
            if (key.is_err()) return rstd::Err(rstd::move(key).unwrap_err_unchecked());
            auto optional = rstd::move(key).unwrap_unchecked();
            if (optional.is_none()) break;
            auto key_value = rstd::move(optional).unwrap_unchecked();
            if (key_value.as_str() == "name"_str) {
                if (name.is_some()) return rstd::Err(deserializer.duplicate_field("name"_str));
                auto value = input.template next_value<String>();
                if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
                name = rstd::Some(rstd::move(value).unwrap_unchecked());
            } else if (key_value.as_str() == "ports"_str) {
                if (ports.is_some()) return rstd::Err(deserializer.duplicate_field("ports"_str));
                auto value = input.template next_value<Vec<rstd::u64>>();
                if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
                ports = rstd::Some(rstd::move(value).unwrap_unchecked());
            } else {
                return rstd::Err(deserializer.unknown_field(key_value.as_str()));
            }
        }
        auto end = input.end();
        if (end.is_err()) return rstd::Err(rstd::move(end).unwrap_err_unchecked());
        if (name.is_none()) return rstd::Err(deserializer.missing_field("name"_str));
        if (ports.is_none()) return rstd::Err(deserializer.missing_field("ports"_str));
        return rstd::Ok(rstd::tests::WireConfig { rstd::move(name).unwrap_unchecked(),
                                                  rstd::move(ports).unwrap_unchecked() });
    }
};
