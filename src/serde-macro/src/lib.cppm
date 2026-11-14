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
    std::optional<std::string>                                    rename, name;
    std::optional<std::string>                                    tag, content;
    std::vector<std::string>                                      aliases;
    std::shared_ptr<Options>                                      parent, variant;
    std::vector<std::pair<std::string, std::shared_ptr<Options>>> cases;
    bool defaulted {}, skip {}, ignore_unknown {}, encode_only {}, decode_only {}, variants {},
        newtype {}, string_enum {}, internal {}, adjacent {}, reject_unknown {},
        described_payload {}, described_enum {};

    auto representation() const -> bool {
        return variants || newtype || string_enum || internal || adjacent || tag || content ||
               described_payload || described_enum || parent || variant || ! cases.empty() || name;
    }
};

auto parse_options(pmacro::TokenStream stream, Options& result, std::size_t begin = 0)
    -> std::expected<void, std::string> {
    for (std::size_t i = begin; i < stream.size(); ++i) {
        const auto token = stream[i];
        if (token.kind() == pmacro::TokenKind::Punct && token.spelling() == ",") continue;
        if (token.kind() != pmacro::TokenKind::Ident)
            return std::unexpected("expected a serde helper option");
        const auto name = token.spelling();
        if (name == "parent" || name == "variant") {
            if (++i == stream.size())
                return std::unexpected("serde parent/variant requires parentheses");
            const auto group = stream[i];
            if (group.kind() != pmacro::TokenKind::Group ||
                group.delimiter() != pmacro::Delimiter::Parenthesis)
                return std::unexpected("serde parent/variant requires parentheses");
            auto arguments = group.children();
            auto nested    = std::make_shared<Options>();
            if (name == "variant" && arguments.size() > 0 &&
                arguments[0].kind() == pmacro::TokenKind::Literal) {
                auto text = std::string(arguments[0].spelling());
                if (text.size() < 3 || text.front() != '"' || text.back() != '"' ||
                    text.find('\\') != std::string::npos)
                    return std::unexpected("serde variant requires a nonempty case name");
                text = text.substr(1, text.size() - 2);
                if (arguments.size() > 1 && arguments[1].spelling() != ",")
                    return std::unexpected("serde variant options require a comma");
                auto parsed = parse_options(arguments, *nested, arguments.size() > 1 ? 2 : 1);
                if (! parsed) return parsed;
                for (const auto& entry : result.cases)
                    if (entry.first == text) return std::unexpected("duplicate serde variant case");
                result.cases.emplace_back(std::move(text), std::move(nested));
            } else {
                auto parsed = parse_options(arguments, *nested);
                if (! parsed) return parsed;
                auto& target = name == "parent" ? result.parent : result.variant;
                if (target) return std::unexpected("duplicate serde " + std::string(name));
                target = std::move(nested);
            }
        } else if (name == "rename" || name == "name" || name == "alias" || name == "tag" ||
                   name == "content") {
            if (++i == stream.size()) return std::unexpected("serde name option requires a string");
            const auto group = stream[i];
            if (group.kind() != pmacro::TokenKind::Group ||
                group.delimiter() != pmacro::Delimiter::Parenthesis)
                return std::unexpected("serde name option requires parentheses");
            const auto arguments = group.children();
            if (arguments.size() != 1 || arguments[0].kind() != pmacro::TokenKind::Literal)
                return std::unexpected("serde name option requires one string literal");
            auto text = std::string(arguments[0].spelling());
            if (text.size() < 3 || text.front() != '"' || text.back() != '"' ||
                text.find('\\') != std::string::npos)
                return std::unexpected("serde names must be nonempty unescaped string literals");
            text = text.substr(1, text.size() - 2);
            if (name == "alias") {
                result.aliases.push_back(text);
            } else {
                auto& target = name == "name"     ? result.name
                               : name == "rename" ? result.rename
                               : name == "tag"    ? result.tag
                                                  : result.content;
                if (target) return std::unexpected("duplicate serde " + std::string(name));
                target = text;
            }
        } else if (name == "default")
            result.defaulted = true;
        else if (name == "skip")
            result.skip = true;
        else if (name == "ignore_unknown")
            result.ignore_unknown = true;
        else if (name == "reject_unknown")
            result.reject_unknown = true;
        else if (name == "encode_only")
            result.encode_only = true;
        else if (name == "decode_only")
            result.decode_only = true;
        else if (name == "variants")
            result.variants = true;
        else if (name == "newtype")
            result.newtype = true;
        else if (name == "string_enum")
            result.string_enum = true;
        else if (name == "internal")
            result.internal = true;
        else if (name == "adjacent")
            result.adjacent = true;
        else if (name == "described_payload")
            result.described_payload = true;
        else if (name == "described_enum")
            result.described_enum = true;
        else
            return std::unexpected("unknown serde option: " + std::string(name));
    }
    return {};
}

auto options(pmacro::Context& context, const std::vector<pmacro::HelperInfo>& helpers)
    -> std::expected<Options, std::string> {
    Options result;
    for (const auto& helper : helpers) {
        if (helper.owner != "serde::derive") continue;
        if (! helper.arguments) return std::unexpected("serde helper arguments are unavailable");
        auto stream = context.tokens(*helper.arguments);
        if (! stream) return std::unexpected("serde helper tokens are unavailable");
        auto parsed = parse_options(*stream, result);
        if (! parsed) return std::unexpected(parsed.error());
    }
    return result;
}

auto valid_variant_options(const Options& settings) -> bool {
    return ! settings.representation() ||
           (settings.name && ! settings.parent && ! settings.variant && settings.cases.empty() &&
            ! settings.variants && ! settings.newtype && ! settings.string_enum &&
            ! settings.internal && ! settings.adjacent && ! settings.tag && ! settings.content &&
            ! settings.described_payload && ! settings.described_enum);
}

auto valid_payload_options(const Options& settings) -> bool {
    return valid_variant_options(settings) && ! settings.defaulted && ! settings.skip &&
           settings.aliases.empty() && ! (settings.encode_only && settings.decode_only) &&
           ! (settings.ignore_unknown && settings.reject_unknown) &&
           ! (settings.name && settings.rename);
}

auto inherit_payload_options(const Options& source, Options& target) -> void {
    if (source.encode_only || source.decode_only) {
        target.encode_only = source.encode_only;
        target.decode_only = source.decode_only;
    }
    if (source.ignore_unknown || source.reject_unknown) {
        target.ignore_unknown = source.ignore_unknown;
        target.reject_unknown = source.reject_unknown;
    }
}

auto derive_payload(pmacro::DeriveInput        input,
                    pmacro::Context&           context,
                    const pmacro::Declaration& declaration,
                    const Options&             config) -> Output {
    const auto* record = std::get_if<pmacro::RecordInfo>(&declaration.data);
    if (! record || record->has_bases || record->kind == pmacro::RecordKind::Union)
        return failure(context,
                       declaration.source,
                       input.item_span,
                       "serde described_payload requires a record without bases");
    if (record->dependent)
        return failure(context,
                       declaration.source,
                       input.item_span,
                       "serde described_payload templates are not supported yet");
    if (config.variants || config.described_enum || config.newtype || config.string_enum ||
        config.tag || config.content || config.internal || config.adjacent || config.rename ||
        ! config.cases.empty())
        return failure(context,
                       declaration.source,
                       input.item_span,
                       "invalid serde described_payload options");
    Options effective;
    if (config.parent) {
        if ((config.parent->encode_only && config.parent->decode_only) ||
            (config.parent->ignore_unknown && config.parent->reject_unknown) ||
            config.parent->parent || config.parent->variant || ! config.parent->cases.empty())
            return failure(context,
                           declaration.source,
                           input.item_span,
                           "invalid serde payload parent options");
        inherit_payload_options(*config.parent, effective);
    }
    inherit_payload_options(config, effective);
    auto name = config.name.value_or(*declaration.name);
    if (config.variant) {
        if (! valid_payload_options(*config.variant))
            return failure(context,
                           declaration.source,
                           input.item_span,
                           "invalid serde payload variant options");
        inherit_payload_options(*config.variant, effective);
        name = config.variant->rename.value_or(config.variant->name.value_or(name));
    }
    auto qualified  = "::" + *declaration.qualified_name;
    auto mode       = effective.encode_only   ? "Serialize"
                      : effective.decode_only ? "Deserialize"
                                              : "Both";
    auto definition = "rstd::serde::DescribedPayloadDefinition<" + qualified + ',' + quote(name) +
                      ",rstd::serde::Direction::" + mode + ",rstd::serde::UnknownFieldPolicy::" +
                      (effective.ignore_unknown ? "Ignore" : "Reject");
    for (const auto& field : record->fields) {
        if (! field.name || field.kind != pmacro::FieldKind::Named ||
            field.access != pmacro::Access::Public || field.bit_width)
            return failure(context,
                           field.source,
                           input.item_span,
                           "serde described_payload requires public named non-bitfield members");
        auto settings = options(context, field.helpers);
        if (! settings) return failure(context, field.source, input.item_span, settings.error());
        if (settings->representation() || settings->encode_only || settings->decode_only ||
            settings->ignore_unknown || settings->reject_unknown ||
            (settings->skip && settings->defaulted))
            return failure(context, field.source, input.item_span, "invalid serde field options");
        if (! settings->rename && ! settings->defaulted && ! settings->skip &&
            settings->aliases.empty())
            continue;
        definition += ",rstd::serde::PayloadMemberOptions<&" + qualified + "::" + *field.name +
                      ',' + quote(settings->rename.value_or(*field.name)) +
                      ",rstd::serde::FieldPolicy::" +
                      (settings->skip        ? "Skip"
                       : settings->defaulted ? "Defaulted"
                                             : "Required");
        for (const auto& alias : settings->aliases) definition += ',' + quote(alias);
        definition += '>';
    }
    return context.parse("constexpr auto rstd_serde_definition(" + qualified + "*) { return " +
                         definition + ">{}; }");
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
        (config->encode_only && config->decode_only) ||
        (config->ignore_unknown && config->reject_unknown))
        return failure(context, declaration.source, input.item_span, "invalid serde type options");
    if (config->described_payload) return derive_payload(input, context, declaration, *config);
    if (config->parent || config->variant || config->name ||
        (! config->described_enum && ! config->cases.empty()))
        return failure(context, declaration.source, input.item_span, "invalid serde type options");
    if (config->described_enum) config->variants = true;
    if ((config->content && ! config->tag) ||
        ((config->tag || config->content || config->internal || config->adjacent) &&
         ! config->variants) ||
        (config->variants && (config->newtype || config->string_enum)) ||
        (config->newtype && config->string_enum) ||
        (config->internal && (config->adjacent || config->content || ! config->tag)) ||
        (config->adjacent && (! config->tag || ! config->content)) ||
        (config->tag && config->content && config->tag == config->content))
        return failure(
            context, declaration.source, input.item_span, "invalid serde representation options");
    auto qualified = "::" + *declaration.qualified_name;
    auto mode = config->encode_only ? "Serialize" : config->decode_only ? "Deserialize" : "Both";
    auto type_name  = config->rename.value_or(*declaration.qualified_name);
    auto source     = std::string {};
    auto definition = std::string {};
    std::set<std::string> names;
    if (config->variants) {
        const auto* record = std::get_if<pmacro::RecordInfo>(&declaration.data);
        if (! record || record->dependent || record->kind == pmacro::RecordKind::Union ||
            (config->ignore_unknown && ! config->tag && ! config->described_enum))
            return failure(context,
                           declaration.source,
                           input.item_span,
                           "serde variants requires a record with VariantDescription");
        definition = "rstd::serde::" +
                     std::string(config->content ? "AdjacentVariantDefinition"
                                 : config->tag   ? "InternalVariantDefinition"
                                                 : "VariantDefinition") +
                     '<' + qualified + ',' + quote(type_name) + ",rstd::serde::Direction::" + mode;
        if (config->tag) definition += ',' + quote(*config->tag);
        if (config->content) definition += ',' + quote(*config->content);
        if (config->tag)
            definition += ",rstd::serde::UnknownFieldPolicy::" +
                          std::string(config->ignore_unknown ? "Ignore" : "Reject");
        for (const auto& entry : config->cases) {
            const auto& settings = *entry.second;
            if (! valid_payload_options(settings))
                return failure(context,
                               declaration.source,
                               input.item_span,
                               "invalid serde enum variant options");
            const auto wire_name = settings.rename.value_or(settings.name.value_or(entry.first));
            if (wire_name != entry.first)
                definition += ",rstd::serde::VariantName<" + quote(entry.first) + ',' +
                              quote(wire_name) + '>';
        }
        for (const auto& entry : config->cases)
            source += "static_assert(" + definition + ">::template contains_case<" +
                      quote(entry.first) + ">(),\"serde variant case does not exist\");";
        return context.parse(source + "constexpr auto rstd_serde_definition(" + qualified +
                             "*) { return " + definition + ">{}; }");
    }
    if (const auto* record = std::get_if<pmacro::RecordInfo>(&declaration.data)) {
        if (config->string_enum)
            return failure(
                context, declaration.source, input.item_span, "serde string_enum requires an enum");
        if (config->newtype && (record->fields.size() != 1 || config->ignore_unknown))
            return failure(context,
                           declaration.source,
                           input.item_span,
                           "serde newtype requires exactly one field and no record options");
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
            if (settings->representation() || settings->encode_only || settings->decode_only ||
                settings->ignore_unknown || settings->reject_unknown ||
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
            if (config->newtype) {
                if (settings->rename || settings->defaulted || settings->skip ||
                    ! settings->aliases.empty())
                    return failure(context,
                                   field.source,
                                   input.item_span,
                                   "serde newtype field does not accept record options");
                definition = "rstd::serde::NewtypeDefinition<" + qualified + ',' +
                             quote(type_name) + ",rstd::serde::Direction::" + mode + ",&" +
                             qualified + "::" + *field.name;
                continue;
            }
            definition += ",rstd::serde::Member<&" + qualified + "::" + *field.name + ',' +
                          quote(name) + ",rstd::serde::FieldPolicy::" +
                          (settings->skip        ? "Skip"
                           : settings->defaulted ? "Defaulted"
                                                 : "Required");
            for (const auto& alias : settings->aliases) definition += ',' + quote(alias);
            definition += '>';
        }
    } else if (const auto* enumeration = std::get_if<pmacro::EnumInfo>(&declaration.data)) {
        if (config->ignore_unknown || config->newtype)
            return failure(
                context, declaration.source, input.item_span, "invalid serde enum representation");
        definition = std::string("rstd::serde::") +
                     (config->string_enum ? "StringEnumDefinition<" : "EnumDefinition<") +
                     qualified + ',' + quote(type_name) + ",rstd::serde::Direction::" + mode;
        for (const auto& item : enumeration->enumerators) {
            auto settings = options(context, item.helpers);
            if (! settings) return failure(context, item.source, input.item_span, settings.error());
            if (settings->representation() || ! settings->aliases.empty() || settings->defaulted ||
                settings->skip || settings->ignore_unknown || settings->reject_unknown ||
                settings->encode_only || settings->decode_only)
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
