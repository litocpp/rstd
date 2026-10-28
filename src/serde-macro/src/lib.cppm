export module rstd.serde.macro;

import std;
import pmacro;

using Output = std::expected<pmacro::TokenStream, pmacro::Error>;

auto failure(pmacro::Context&          context,
             const pmacro::SourceInfo& source,
             pmacro::Span              fallback,
             std::string_view          message) -> Output {
    context.diagnostic(
        pmacro::DiagnosticLevel::Error, source.diagnostic.value_or(fallback), message);
    return std::unexpected(pmacro::Error::Failure);
}

auto quote(std::string_view text) -> std::string {
    std::string output = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') output += '\\';
        output += c;
    }
    return output + '"';
}

struct Options {
    std::optional<std::string> rename;
    std::vector<std::string>   aliases;
    bool defaulted {}, skip {}, ignore_unknown {}, encode_only {}, decode_only {};
};

auto options(pmacro::Context& context, const std::vector<pmacro::HelperInfo>& helpers)
    -> std::expected<Options, std::string> {
    Options result;
    for (const auto& helper : helpers) {
        if (helper.owner != "serde::derive") continue;
        if (! helper.arguments) return std::unexpected("serde helper arguments are unavailable");
        auto stream = context.tokens(*helper.arguments);
        if (! stream) return std::unexpected("serde helper tokens are unavailable");
        for (std::size_t i = 0; i < stream->size(); ++i) {
            const auto token = (*stream)[i];
            if (token.kind() == pmacro::TokenKind::Punct && token.spelling() == ",") continue;
            if (token.kind() != pmacro::TokenKind::Ident)
                return std::unexpected("expected a serde helper option");
            const auto name = token.spelling();
            if (name == "rename" || name == "alias") {
                if (++i == stream->size())
                    return std::unexpected("serde rename/alias requires a string");
                const auto group = (*stream)[i];
                if (group.kind() != pmacro::TokenKind::Group ||
                    group.delimiter() != pmacro::Delimiter::Parenthesis)
                    return std::unexpected("serde rename/alias requires parentheses");
                const auto arguments = group.children();
                if (arguments.size() != 1 || arguments[0].kind() != pmacro::TokenKind::Literal)
                    return std::unexpected("serde rename/alias requires one string literal");
                auto text = std::string(arguments[0].spelling());
                if (text.size() < 3 || text.front() != '"' || text.back() != '"' ||
                    text.find('\\') != std::string::npos)
                    return std::unexpected(
                        "serde names must be nonempty unescaped string literals");
                text = text.substr(1, text.size() - 2);
                if (name == "rename") {
                    if (result.rename) return std::unexpected("duplicate serde rename");
                    result.rename = text;
                } else
                    result.aliases.push_back(text);
            } else if (name == "default")
                result.defaulted = true;
            else if (name == "skip")
                result.skip = true;
            else if (name == "ignore_unknown")
                result.ignore_unknown = true;
            else if (name == "encode_only")
                result.encode_only = true;
            else if (name == "decode_only")
                result.decode_only = true;
            else
                return std::unexpected("unknown serde option: " + std::string(name));
        }
    }
    return result;
}

[[pmacro::define("serde")]]
auto derive(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    auto snapshot = context.declaration();
    if (! snapshot)
        return failure(
            context, {}, input.item_span, "serde derive requires a complete declaration");
    const auto& declaration = **snapshot;
    if (! declaration.name || ! declaration.qualified_name)
        return failure(
            context, declaration.source, input.item_span, "serde derive requires a named type");
    auto config = options(context, declaration.helpers);
    if (! config) return failure(context, declaration.source, input.item_span, config.error());
    if (config->skip || config->defaulted || ! config->aliases.empty() ||
        (config->encode_only && config->decode_only))
        return failure(context, declaration.source, input.item_span, "invalid serde type options");
    auto qualified = "::" + *declaration.qualified_name;
    auto mode = config->encode_only ? "Serialize" : config->decode_only ? "Deserialize" : "Both";
    auto type_name  = config->rename.value_or(*declaration.qualified_name);
    auto source     = std::string {};
    auto definition = std::string {};
    std::set<std::string> names;
    if (const auto* record = std::get_if<pmacro::RecordInfo>(&declaration.data)) {
        if (record->dependent || record->has_bases || record->kind == pmacro::RecordKind::Union)
            return failure(context,
                           declaration.source,
                           input.item_span,
                           "serde derive requires a non-template aggregate without bases");
        source += "static_assert(__is_aggregate(" + qualified +
                  "),\"serde derive requires an aggregate\");";
        definition = "rstd::serde::RecordDefinition<" + qualified + ',' + quote(type_name) +
                     ",rstd::serde::Direction::" + mode + ",rstd::serde::UnknownFieldPolicy::" +
                     (config->ignore_unknown ? "Ignore" : "Reject");
        for (const auto& field : record->fields) {
            if (! field.name || field.kind != pmacro::FieldKind::Named ||
                field.access != pmacro::Access::Public || field.bit_width || field.type.dependent ||
                ! field.type.type_id)
                return failure(context,
                               field.source,
                               input.item_span,
                               "serde derive requires public named non-bitfield members");
            auto settings = options(context, field.helpers);
            if (! settings)
                return failure(context, field.source, input.item_span, settings.error());
            if (settings->encode_only || settings->decode_only || settings->ignore_unknown ||
                (settings->skip && settings->defaulted))
                return failure(
                    context, field.source, input.item_span, "invalid serde field options");
            auto name = settings->rename.value_or(*field.name);
            if (! settings->skip) {
                if (! names.insert(name).second)
                    return failure(
                        context, field.source, input.item_span, "serde field names overlap");
                for (const auto& alias : settings->aliases)
                    if (! names.insert(alias).second)
                        return failure(
                            context, field.source, input.item_span, "serde field names overlap");
            }
            auto member_type = "decltype(" + qualified + "::" + *field.name + ")";
            source += "static_assert(!__is_reference(" + member_type + ")&&!__is_const(" +
                      member_type +
                      "),\"serde derive does not support const or reference fields\");";
            definition += ",rstd::serde::Member<&" + qualified + "::" + *field.name + ',' +
                          quote(name) + ",rstd::serde::FieldPolicy::" +
                          (settings->skip        ? "Skip"
                           : settings->defaulted ? "Defaulted"
                                                 : "Required");
            for (const auto& alias : settings->aliases) definition += ',' + quote(alias);
            definition += '>';
        }
    } else if (const auto* enumeration = std::get_if<pmacro::EnumInfo>(&declaration.data)) {
        if (config->ignore_unknown)
            return failure(context,
                           declaration.source,
                           input.item_span,
                           "ignore_unknown applies only to records");
        definition = "rstd::serde::EnumDefinition<" + qualified + ',' + quote(type_name) +
                     ",rstd::serde::Direction::" + mode;
        for (const auto& item : enumeration->enumerators) {
            auto settings = options(context, item.helpers);
            if (! settings) return failure(context, item.source, input.item_span, settings.error());
            if (! settings->aliases.empty() || settings->defaulted || settings->skip ||
                settings->ignore_unknown || settings->encode_only || settings->decode_only)
                return failure(
                    context, item.source, input.item_span, "enum items support only serde rename");
            auto name = settings->rename.value_or(item.name);
            if (! names.insert(name).second)
                return failure(context, item.source, input.item_span, "duplicate serde enum name");
            definition +=
                ",rstd::serde::Variant<" + qualified + "::" + item.name + ',' + quote(name) + '>';
        }
    } else
        return failure(
            context, declaration.source, input.item_span, "serde derive requires a record or enum");
    source += "constexpr auto rstd_serde_definition(" + qualified + "*) { return " + definition +
              ">{}; }";
    return context.parse(source);
}
