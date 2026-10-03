module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;
import rstd.json;
import rstd.toml;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace bench = rstd::bench;

auto domain_descriptor(ref<str> suite, ref<str> name) -> bench::CaseDescriptor {
    return rstd_bench::make_descriptor(suite, name, u64(3));
}

auto data_descriptor(ref<str> id, ref<str> data) -> bench::DatasetDescriptor {
    rstd::hash::DefaultHasher fingerprint;
    fingerprint.write(data.as_bytes());
    return { String::make(id),
             "1"_Str,
             "siphash-1-3/zero-key"_Str,
             rstd::format("{:016x}", fingerprint.finish()),
             u64(data.len().to_primitive()),
             None() };
}

template<bool Json>
auto parse_document(ref<str> source) {
    if constexpr (Json)
        return rstd::json::from_str(source);
    else
        return rstd::toml::from_str(source);
}

template<bool Json, typename Document>
auto encode_document(const Document& document) -> Result<String, String> {
    if constexpr (Json)
        return Ok(rstd::json::to_string(document));
    else {
        auto encoded = rstd::toml::to_string(document);
        if (encoded.is_err()) return Err(rstd::format("{}", encoded.unwrap_err()));
        return Ok(rstd::move(encoded).unwrap());
    }
}

template<bool Json>
auto add_document(bench::Suite& suite, ref<str> label, ref<str> source) -> Result<empty, String> {
    auto       category = Json ? "json"_str : "toml"_str;
    const bool modes[] { false, true };
    for (bool serialize : modes) {
        auto name = rstd::format("{}/{}", serialize ? "serialize"_str : "parse-owned"_str, label);
        auto descriptor    = domain_descriptor(category, name.as_str());
        descriptor.dataset = Some(data_descriptor(label, source));
        descriptor.parameters.push(
            { "bytes_basis"_Str,
              bench::ParameterValue::Text(serialize ? "output"_Str : "input"_Str) });
        auto added = suite.add_function(
            rstd::move(descriptor), [source, serialize](bench::Bench& engine, ref<str> name) {
                auto parsed = parse_document<Json>(source);
                if (parsed.is_err())
                    return bench::failed(rstd::format("parse precheck: {}", parsed.unwrap_err()));
                auto answer = parsed->get("answer"_str);
                if (answer.is_none()) return bench::failed("missing answer"_Str);
                auto valid = [&] {
                    if constexpr (Json)
                        return (*answer)->as_i64() == Some(i64(42));
                    else
                        return (*answer)->as_integer() == Some(i64(42));
                }();
                if (! valid) return bench::failed("wrong answer"_Str);
                auto encoded = encode_document<Json>(*parsed);
                if (encoded.is_err()) return bench::failed(rstd::move(encoded).unwrap_err());
                auto roundtrip = parse_document<Json>(encoded->as_str());
                if (roundtrip.is_err() || ! (*roundtrip == *parsed))
                    return bench::failed("roundtrip differs"_Str);
                bench::RunConfig work;
                work.unit                = "document"_Str;
                work.items_per_iteration = u64(1);
                work.bytes_per_iteration =
                    u64((serialize ? encoded->len() : source.len()).to_primitive());
                if (serialize)
                    return bench::complete_measurement(engine.run(
                                                           name,
                                                           [&] {
                                                               return encode_document<Json>(
                                                                   *parsed);
                                                           },
                                                           rstd::move(work)),
                                                       Ok(empty {}));
                return bench::complete_measurement(engine.run(
                                                       name,
                                                       [&] {
                                                           return parse_document<Json>(
                                                               rstd::hint::black_box(source));
                                                       },
                                                       rstd::move(work)),
                                                   Ok(empty {}));
            });
        if (added.is_err()) return added;
    }
    return Ok(empty {});
}

auto hash_input(slice<u8> bytes, bool streaming) -> u64 {
    rstd::hash::DefaultHasher hash;
    if (streaming) {
        for (usize offset; offset < bytes.len();) {
            auto size = (bytes.len() - offset).min(usize(13));
            hash.write(slice<u8>::from_raw_parts(bytes.as_raw_ptr() + offset.to_primitive(), size));
            offset += size;
        }
    } else
        hash.write(bytes);
    return hash.finish();
}

auto add_hash_case(bench::Suite& suite, usize size, bool streaming) -> Result<empty, String> {
    auto descriptor = domain_descriptor(
        "hash"_str,
        rstd::format("{}/{}", streaming ? "stream-13"_str : "one-shot"_str, size).as_str());
    descriptor.parameters.push(
        { "bytes"_Str, bench::ParameterValue::Unsigned(u64(size.to_primitive())) });
    descriptor.parameters.push({ "cache"_Str, bench::ParameterValue::Text("reused-buffer"_Str) });
    return suite.add_function(
        rstd::move(descriptor), [size, streaming](bench::Bench& engine, ref<str> name) {
            Vec<u8> input;
            for (usize i; i < size; ++i) input.push(u8(i.to_primitive() & 255u));
            if (hash_input(input.as_slice(), false) != hash_input(input.as_slice(), true))
                return bench::failed("streaming hash differs"_Str);
            bench::RunConfig work;
            work.unit                = "hash"_Str;
            work.items_per_iteration = u64(1);
            work.bytes_per_iteration = u64(size.to_primitive());
            return bench::complete_measurement(
                engine.run(
                    name,
                    [&] {
                        return hash_input(rstd::hint::black_box(input.as_slice()), streaming);
                    },
                    rstd::move(work)),
                Ok(empty {}));
        });
}

auto rstd_bench::add_domain_cases(bench::Suite& suite) -> Result<empty, String> {
    rstd_try(add_document<true>(
        suite, "config"_str, R"({"answer":42,"name":"lito","enabled":true})"_str));
    rstd_try(add_document<true>(
        suite, "numbers"_str, R"({"answer":42,"values":[0,1,-2,3.5,4e20,5,6,7,8,9]})"_str));
    rstd_try(add_document<true>(
        suite,
        "text"_str,
        R"({"answer":42,"name":"中文\n\u0061\"\\","nested":{"values":[true,false,null]}})"_str));
    rstd_try(add_document<false>(
        suite, "config"_str, "answer = 42\n[package]\nname = 'example'\nversion = '0.1.0'\n"_str));
    rstd_try(add_document<false>(
        suite,
        "tables"_str,
        "answer = 42\n[[packages]]\nname = 'one'\n[[packages]]\nname = 'two'\n"_str));
    rstd_try(add_document<false>(
        suite, "text"_str, "answer = 42\nname = \"中文\\ntext\"\nvalues = [1, 2, 3, 4]\n"_str));
    rstd_try(add_collection_cases(suite));
    const rstd::size_t sizes[] { 0, 7, 8, 9, 15, 16, 17, 64, 4096 };
    for (auto size : sizes) {
        rstd_try(add_hash_case(suite, usize(size), false));
        rstd_try(add_hash_case(suite, usize(size), true));
    }
    return Ok(empty {});
}
