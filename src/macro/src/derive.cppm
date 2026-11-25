export module rstd.macro:derive;

import std;
import pmacro;

export namespace rstd::macro
{
using Output = std::expected<pmacro::TokenStream, pmacro::Error>;

struct Error {
    pmacro::SourceInfo source;
    std::string        message;
};

template<typename T>
using Checked = std::expected<T, Error>;

auto failure(pmacro::Context& context, pmacro::Span fallback, const Error& error) -> Output {
    context.diagnostic(
        pmacro::DiagnosticLevel::Error, error.source.diagnostic.value_or(fallback), error.message);
    return std::unexpected(pmacro::Error::Failure);
}

auto quote(std::string_view text) -> std::string {
    std::string output = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            output += '\\';
            output += static_cast<char>(c);
        } else if (c < 32 || c == 127) {
            output += '\\';
            output += static_cast<char>('0' + ((c >> 6) & 7));
            output += static_cast<char>('0' + ((c >> 3) & 7));
            output += static_cast<char>('0' + (c & 7));
        } else
            output += static_cast<char>(c);
    }
    return output + '"';
}

auto string_literal(std::string_view spelling, bool escapes = false)
    -> std::expected<std::string, std::string> {
    if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"' ||
        (! escapes && spelling.find('\\') != std::string_view::npos))
        return std::unexpected("expected one unescaped ordinary narrow string literal; "
                               "prefixes, raw strings and concatenation are not supported");
    std::string result;
    for (std::size_t i = 1; i + 1 < spelling.size(); ++i) {
        auto c = spelling[i];
        if (c != '\\') {
            result += c;
            continue;
        }
        if (++i + 1 >= spelling.size())
            return std::unexpected("unterminated escape in format string");
        switch (spelling[i]) {
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case '\\': result += '\\'; break;
        case '"': result += '"'; break;
        default: return std::unexpected("unsupported escape in format string");
        }
    }
    return result;
}

enum class Trait
{
    Clone,
    Debug,
    Display,
    Error,
    FromStr,
    EnumIter,
    TryFrom
};

enum class Target
{
    Record,
    Enum,
    Field,
    Enumerator,
    Method
};

auto owner(Trait trait) -> std::string_view {
    switch (trait) {
    case Trait::Clone: return "rstd::Clone";
    case Trait::Debug: return "rstd::Debug";
    case Trait::Display: return "rstd::Display";
    case Trait::Error: return "rstd::Error";
    case Trait::FromStr: return "rstd::FromStr";
    case Trait::EnumIter: return "rstd::EnumIter";
    case Trait::TryFrom: return "rstd::TryFrom";
    }
    std::unreachable();
}

auto target_name(Target target) -> std::string_view {
    switch (target) {
    case Target::Record: return "record types";
    case Target::Enum: return "enum types";
    case Target::Field: return "record fields";
    case Target::Enumerator: return "enum items";
    case Target::Method: return "methods";
    }
    std::unreachable();
}

auto allowed(Trait trait, Target target, std::string_view option) -> bool {
    switch (trait) {
    case Trait::Clone: return target == Target::Record && option == "variants";
    case Trait::Debug:
        return ((target == Target::Record || target == Target::Enum || target == Target::Field ||
                 target == Target::Enumerator) &&
                option == "rename") ||
               (target == Target::Field && option == "skip") ||
               (target == Target::Record && (option == "variants" || option == "tuple"));
    case Trait::Display:
        return (target == Target::Record && (option == "format" || option == "transparent" ||
                                             option == "variants" || option == "unit")) ||
               (target == Target::Enumerator && option == "rename");
    case Trait::Error:
        return (target == Target::Record && (option == "format" || option == "transparent" ||
                                             option == "variants" || option == "unit")) ||
               (target == Target::Field && (option == "source" || option == "from"));
    case Trait::FromStr:
        return (target == Target::Record && option == "transparent") ||
               (target == Target::Enum && option == "ascii_case_insensitive") ||
               (target == Target::Enumerator && (option == "rename" || option == "alias"));
    case Trait::EnumIter:
    case Trait::TryFrom: return false;
    }
    std::unreachable();
}

struct Options {
    std::optional<std::string>                       rename;
    std::optional<std::string>                       format;
    std::vector<std::string>                         aliases;
    std::vector<std::pair<std::string, std::string>> units;
    bool skip {}, transparent {}, ascii_case_insensitive {}, variants {}, tuple {}, source {},
        from {};
};

struct HelperTokens {
    pmacro::TokenStream arguments;
    pmacro::SourceInfo  source;
};

auto options(const std::vector<HelperTokens>& helpers, Trait trait, Target target)
    -> Checked<Options> {
    Options               result;
    std::set<std::string> seen;
    for (const auto& helper : helpers) {
        const auto fail = [&](std::string message) -> Checked<Options> {
            return std::unexpected(
                Error { helper.source, std::string(owner(trait)) + ": " + message });
        };
        auto stream = std::optional(helper.arguments);
        if (! stream->valid()) return fail("helper tokens are unavailable");
        if (stream->size() == 0) return fail("helper requires at least one option");
        std::size_t i {};
        while (i < stream->size()) {
            const auto token = (*stream)[i++];
            if (token.kind() != pmacro::TokenKind::Ident) return fail("expected a helper option");
            const auto name = std::string(token.spelling());
            if (! allowed(trait, target, name))
                return fail("unknown or invalid option '" + name + "' on " +
                            std::string(target_name(target)));
            if (name != "alias" && name != "unit" && ! seen.insert(name).second)
                return fail("duplicate option '" + name + "'");
            if (name == "unit") {
                if (i == stream->size()) return fail("unit requires a variant name and format");
                const auto group = (*stream)[i++];
                if (group.kind() != pmacro::TokenKind::Group ||
                    group.delimiter() != pmacro::Delimiter::Parenthesis)
                    return fail("unit requires parentheses");
                const auto arguments = group.children();
                if (arguments.size() != 3 || arguments[0].kind() != pmacro::TokenKind::Literal ||
                    arguments[1].spelling() != "," ||
                    arguments[2].kind() != pmacro::TokenKind::Literal)
                    return fail("unit requires two string literals");
                auto name   = string_literal(arguments[0].spelling());
                auto format = string_literal(arguments[2].spelling(), true);
                if (! name) return fail("unit variant: " + name.error());
                if (! format) return fail("unit format: " + format.error());
                if (name->empty()) return fail("unit requires a nonempty variant name");
                if (! seen.insert("unit:" + *name).second)
                    return fail("duplicate unit format '" + *name + "'");
                result.units.emplace_back(std::move(*name), std::move(*format));
            } else if (name == "rename" || name == "alias" || name == "format") {
                if (i == stream->size()) return fail("option '" + name + "' requires a string");
                const auto group = (*stream)[i++];
                if (group.kind() != pmacro::TokenKind::Group ||
                    group.delimiter() != pmacro::Delimiter::Parenthesis)
                    return fail("option '" + name + "' requires parentheses");
                const auto arguments = group.children();
                if (arguments.size() != 1 || arguments[0].kind() != pmacro::TokenKind::Literal)
                    return fail("option '" + name + "' requires one string literal");
                auto text = string_literal(arguments[0].spelling(), name == "format");
                if (! text) return fail("option '" + name + "': " + text.error());
                if (name != "format" && (text->empty() || text->find('\0') != std::string::npos))
                    return fail("option '" + name + "' requires a nonempty name without NUL");
                if (name == "rename")
                    result.rename = std::move(*text);
                else if (name == "format")
                    result.format = std::move(*text);
                else
                    result.aliases.push_back(std::move(*text));
            } else if (name == "skip")
                result.skip = true;
            else if (name == "transparent")
                result.transparent = true;
            else if (name == "variants")
                result.variants = true;
            else if (name == "tuple")
                result.tuple = true;
            else if (name == "source")
                result.source = true;
            else if (name == "from")
                result.from = true;
            else
                result.ascii_case_insensitive = true;
            if (i == stream->size()) break;
            const auto separator = (*stream)[i++];
            if (separator.kind() != pmacro::TokenKind::Punct || separator.spelling() != ",")
                return fail("expected ',' between helper options");
        }
        if (result.skip && result.rename) return fail("skip and rename cannot be combined");
        if (result.transparent && result.format)
            return fail("transparent and format cannot be combined");
    }
    if (result.variants && (result.transparent || result.format || result.tuple))
        return std::unexpected(
            Error { {},
                    std::string(owner(trait)) +
                        ": variants cannot be combined with transparent, format or tuple" });
    if (! result.variants && ! result.units.empty())
        return std::unexpected(
            Error { {}, std::string(owner(trait)) + ": unit requires variants" });
    return result;
}

auto options(pmacro::Context&                       context,
             const std::vector<pmacro::HelperInfo>& helpers,
             Trait                                  trait,
             Target                                 target) -> Checked<Options> {
    std::vector<HelperTokens> inputs;
    for (const auto& helper : helpers) {
        if (helper.owner != owner(trait)) continue;
        if (! helper.arguments)
            return std::unexpected(Error {
                helper.source, std::string(owner(trait)) + ": helper arguments are unavailable" });
        auto tokens = context.tokens(*helper.arguments);
        if (! tokens)
            return std::unexpected(Error {
                helper.source, std::string(owner(trait)) + ": helper tokens are unavailable" });
        inputs.push_back({ *tokens, helper.source });
    }
    return options(inputs, trait, target);
}

struct Field {
    std::string name;
    std::string definition;
};

struct MappedFormat {
    std::string              text;
    std::vector<std::size_t> fields;
};

auto map_format(std::string_view format, const std::vector<Field>& fields, bool positional = false)
    -> std::expected<MappedFormat, std::string> {
    MappedFormat result;
    auto&        output = result.text;
    for (std::size_t i = 0; i < format.size();) {
        if (format[i] != '{') {
            output += format[i++];
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == '{') {
            output += "{{";
            i += 2;
            continue;
        }
        const auto end = format.find_first_of(":{}", i + 1);
        // Leave malformed format syntax to DisplayRecord's fmt validation.
        if (end == std::string_view::npos) {
            output += format.substr(i);
            break;
        }
        if (format[end] == '{') {
            output += format[i++];
            continue;
        }
        const auto  name = format.substr(i + 1, end - i - 1);
        std::size_t index {};
        if (positional && ! name.empty() && std::ranges::all_of(name, [](char c) {
                return c >= '0' && c <= '9';
            })) {
            [[unsafe]] {
                const auto parsed = std::from_chars(name.data(), name.data() + name.size(), index);
                if (parsed.ec != std::errc() || index >= fields.size())
                    return std::unexpected("field index is out of range in Display format");
            }
        } else {
            while (index < fields.size() && fields[index].name != name) ++index;
        }
        if (index == fields.size())
            return std::unexpected("unknown field '" + std::string(name) + "' in Display format");
        auto selected = std::find(result.fields.begin(), result.fields.end(), index);
        if (selected == result.fields.end()) {
            result.fields.push_back(index);
            selected = result.fields.end() - 1;
        }
        output += '{';
        output += std::to_string(selected - result.fields.begin());
        i = end;
    }
    return result;
}

struct Description {
    std::string definition;
    std::string assertions;
    std::string display_definition;
};

auto error_record_description(const std::string&         qualified,
                              const Options&             config,
                              const std::vector<Field>&  fields,
                              std::optional<std::size_t> source,
                              bool                       from,
                              bool positional = false) -> std::expected<Description, std::string> {
    if (config.transparent && fields.size() != 1)
        return std::unexpected("transparent requires exactly one field");
    if (! config.transparent && ! config.format)
        return std::unexpected("record requires format(\"...\") or transparent");
    if (from && fields.size() != 1) return std::unexpected("from requires exactly one field");
    if (config.transparent) source = 0;
    Description result;
    result.definition = "::rstd::derive::ErrorRecord<" + qualified + ',' +
                        (config.transparent ? "true" : "false") + ',' +
                        (source ? fields[*source].definition : "void") + ',' +
                        (from ? "true" : "false") + '>';
    if (config.transparent)
        result.display_definition =
            "::rstd::derive::TransparentDisplay<" + qualified + ',' + fields[0].definition + '>';
    else {
        auto format = map_format(*config.format, fields, positional);
        if (! format) return std::unexpected(format.error());
        result.display_definition =
            "::rstd::derive::DisplayRecord<" + qualified + ',' + quote(format->text);
        for (auto index : format->fields)
            result.display_definition += ',' + fields[index].definition;
        result.display_definition += '>';
    }
    return result;
}

auto record_description(pmacro::Context&           context,
                        const pmacro::Declaration& declaration,
                        const pmacro::RecordInfo&  record,
                        Trait                      trait,
                        const Options&             config,
                        const std::string&         qualified) -> Checked<Description> {
    const auto fail = [&](const pmacro::SourceInfo& source,
                          std::string               message) -> Checked<Description> {
        return std::unexpected(Error { source, std::string(owner(trait)) + ": " + message });
    };
    if (record.kind == pmacro::RecordKind::Union)
        return fail(declaration.source, "unions are not supported");
    if (record.dependent) return fail(declaration.source, "dependent records are not supported");
    if (record.has_bases) return fail(declaration.source, "records with bases are not supported");
    if (trait == Trait::Clone && config.variants)
        return Description { "::rstd::derive::CloneVariants<" + qualified + '>', {} };
    if (config.variants) {
        auto        name = trait == Trait::Debug   ? "DebugVariants"
                           : trait == Trait::Error ? "ErrorVariants"
                                                   : "DisplayVariants";
        Description result { "::rstd::derive::" + std::string(name) + '<' + qualified, {}, {} };
        for (const auto& [unit, format] : config.units)
            result.definition +=
                ",::rstd::derive::UnitFormat<" + quote(unit) + ',' + quote(format) + '>';
        result.definition += '>';
        if (trait == Trait::Error) result.display_definition = result.definition;
        return result;
    }
    if ((trait == Trait::Display || trait == Trait::Error) && ! config.format &&
        ! config.transparent)
        return fail(declaration.source, "record requires format(\"...\") or transparent");
    if (trait == Trait::FromStr && ! config.transparent)
        return fail(declaration.source, "record requires explicit transparent");
    if (config.transparent && record.fields.size() != 1)
        return fail(declaration.source, "transparent requires exactly one field");

    std::vector<Field>         fields;
    std::optional<std::size_t> source;
    bool                       from = false;
    std::set<std::string>      names;
    for (const auto& field : record.fields) {
        auto settings = options(context, field.helpers, trait, Target::Field);
        if (! settings) return std::unexpected(settings.error());
        if (field.kind == pmacro::FieldKind::AnonymousRecord ||
            (field.kind == pmacro::FieldKind::Named && ! field.name))
            return fail(field.source, "anonymous fields are not supported");
        if (field.bit_width || field.kind == pmacro::FieldKind::UnnamedBitField)
            return fail(field.source, "bitfields are not supported");
        if (field.type.dependent) return fail(field.source, "dependent fields are not supported");
        if (settings->skip) continue;
        if (trait != Trait::Clone && field.access != pmacro::Access::Public)
            return fail(field.source, "non-skipped fields must be public");
        const auto name = settings->rename.value_or(*field.name);
        if (! names.insert(name).second)
            return fail(field.source, "duplicate field name '" + name + "'");
        fields.push_back(Field { *field.name,
                                 "::rstd::derive::Field<" + quote(name) + ",[](const " + qualified +
                                     "& v)->decltype(auto){return (v." + *field.name + ");}>" });
        if (trait == Trait::Error &&
            (settings->source || settings->from || *field.name == "source")) {
            if (source) return fail(field.source, "multiple source fields are not supported");
            if (config.transparent && settings->source)
                return fail(field.source, "transparent cannot contain an explicit source field");
            source = fields.size() - 1;
            from   = settings->from;
        }
    }
    for (const auto& method : record.methods) {
        auto settings = options(context, method.helpers, trait, Target::Method);
        if (! settings) return std::unexpected(settings.error());
    }

    Description result;
    if (trait == Trait::Error) {
        auto description = error_record_description(qualified, config, fields, source, from);
        if (! description) return fail(declaration.source, description.error());
        return *description;
    }
    if (trait == Trait::Clone) {
        result.definition = "::rstd::derive::CloneRecord<" + qualified + ",[](auto&&... fields)->" +
                            qualified + "{return " + qualified +
                            "{::rstd::forward<decltype(fields)>(fields)...};}";
        for (const auto& field : fields) {
            const auto type = "decltype(" + qualified + "::" + field.name + ")";
            result.assertions += "static_assert(!__is_reference(" + type + ")&&!__is_const(" +
                                 type +
                                 "),\"rstd::Clone fields must not be const or references\");";
        }
    } else if (trait == Trait::Debug) {
        result.definition = std::string(config.tuple ? "::rstd::derive::DebugTuple<"
                                                     : "::rstd::derive::DebugRecord<") +
                            qualified + ',' + quote(config.rename.value_or(*declaration.name));
    } else if (config.transparent) {
        result.definition = trait == Trait::Display ? "::rstd::derive::TransparentDisplay<"
                                                    : "::rstd::derive::TransparentFromStr<";
        result.definition += qualified + ',' + fields[0].definition + '>';
        if (trait == Trait::FromStr)
            result.assertions = "static_assert(!__is_reference(decltype(" + qualified +
                                "::" + fields[0].name +
                                ")),\"rstd::FromStr transparent field must not be a reference\");";
        return result;
    } else {
        auto format = map_format(*config.format, fields);
        if (! format) return fail(declaration.source, format.error());
        result.definition =
            "::rstd::derive::DisplayRecord<" + qualified + ',' + quote(format->text);
        for (auto index : format->fields) result.definition += ',' + fields[index].definition;
        result.definition += '>';
        return result;
    }
    for (const auto& field : fields) result.definition += ',' + field.definition;
    result.definition += '>';
    return result;
}

auto normalized(std::string name, bool insensitive) -> std::string {
    if (insensitive)
        for (char& c : name)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return name;
}

auto enum_description(pmacro::Context&           context,
                      const pmacro::Declaration& declaration,
                      const pmacro::EnumInfo&    enumeration,
                      Trait                      trait,
                      const Options&             config,
                      const std::string&         qualified) -> Checked<Description> {
    if (enumeration.underlying_type.dependent)
        return std::unexpected(
            Error { declaration.source,
                    std::string(owner(trait)) + ": dependent enums are not supported" });
    Description result;
    if (trait == Trait::Clone)
        return Description { "::rstd::derive::CloneEnum<" + qualified + '>', {} };
    const auto values = trait == Trait::EnumIter || trait == Trait::TryFrom;
    if (values)
        result.definition =
            "::rstd::derive::EnumValues<" + qualified + ',' + quote(*declaration.name);
    else if (trait == Trait::FromStr)
        result.definition = "::rstd::derive::EnumFromStr<" + qualified + ',' +
                            (config.ascii_case_insensitive ? "true" : "false");
    else
        result.definition = "::rstd::derive::EnumFormat<" + qualified + ',' +
                            quote(config.rename.value_or(*declaration.name));
    std::set<std::string> names;
    for (const auto& item : enumeration.enumerators) {
        auto settings = options(context, item.helpers, trait, Target::Enumerator);
        if (! settings) return std::unexpected(settings.error());
        if (values) {
            result.definition += ',' + qualified + "::" + item.name;
            continue;
        }
        const auto name     = settings->rename.value_or(item.name);
        const auto add_name = [&](const std::string& text) -> Checked<void> {
            if (! names.insert(normalized(text, config.ascii_case_insensitive)).second)
                return std::unexpected(Error {
                    item.source,
                    std::string(owner(trait)) + ": enum name or alias collision for '" + text +
                        (config.ascii_case_insensitive ? "' after ASCII case folding" : "'") });
            return {};
        };
        auto added = add_name(name);
        if (! added) return std::unexpected(added.error());
        result.definition +=
            ",::rstd::derive::EnumEntry<" + qualified + "::" + item.name + ',' + quote(name);
        for (const auto& alias : settings->aliases) {
            added = add_name(alias);
            if (! added) return std::unexpected(added.error());
            result.definition += ',' + quote(alias);
        }
        result.definition += '>';
    }
    result.definition += '>';
    return result;
}

auto derive(pmacro::DeriveInput input, pmacro::Context& context, Trait trait) -> Output {
    auto       snapshot = context.declaration();
    const auto fail     = [&](const pmacro::SourceInfo& source, std::string message) -> Output {
        return failure(
            context, input.item_span, Error { source, std::string(owner(trait)) + ": " + message });
    };
    if (! snapshot) return fail({}, "derive requires a complete declaration");
    const auto& declaration = **snapshot;
    if (! declaration.name || ! declaration.qualified_name)
        return fail(declaration.source, "derive requires a named type");
    if (declaration.scope == pmacro::DeclarationScope::Record)
        return fail(declaration.source, "nested declarations in record scope are not supported");
    if (declaration.scope != pmacro::DeclarationScope::Namespace)
        return fail(declaration.source, "derive requires namespace scope");
    const auto record      = std::holds_alternative<pmacro::RecordInfo>(declaration.data);
    const auto enumeration = std::holds_alternative<pmacro::EnumInfo>(declaration.data);
    if (! record && ! enumeration)
        return fail(declaration.source, "derive requires a record or enum");
    if (record && (trait == Trait::EnumIter || trait == Trait::TryFrom))
        return fail(declaration.source, "derive requires an enum");
    if (enumeration && trait == Trait::Error)
        return fail(declaration.source, "Error requires a record or described variants");
    auto config =
        options(context, declaration.helpers, trait, record ? Target::Record : Target::Enum);
    if (! config) return failure(context, input.item_span, config.error());
    const auto qualified = "::" + *declaration.qualified_name;
    auto description = record ? record_description(context,
                                                   declaration,
                                                   std::get<pmacro::RecordInfo>(declaration.data),
                                                   trait,
                                                   *config,
                                                   qualified)
                              : enum_description(context,
                                                 declaration,
                                                 std::get<pmacro::EnumInfo>(declaration.data),
                                                 trait,
                                                 *config,
                                                 qualified);
    if (! description) return failure(context, input.item_span, description.error());
    const auto function = [&] {
        switch (trait) {
        case Trait::Clone: return "rstd_clone_definition";
        case Trait::Debug: return "rstd_debug_definition";
        case Trait::Display: return "rstd_display_definition";
        case Trait::Error: return "rstd_error_definition";
        case Trait::FromStr: return "rstd_from_str_definition";
        case Trait::EnumIter: return "rstd_enum_iter_definition";
        case Trait::TryFrom: return "rstd_enum_try_from_definition";
        }
        std::unreachable();
    }();
    std::string source;
    if (declaration.after_item_export == pmacro::AfterItemExport::Required) source += "export ";
    // Clone definitions must share the enclosing module with their friend declarations.
    if (trait != Trait::Clone) source += "extern \"C++\" { ";
    source += "constexpr auto ";
    source += function;
    source += '(' + qualified + "*) {" + description->assertions + "return " +
              description->definition + "{}; }";
    if (! description->display_definition.empty())
        source += "constexpr auto rstd_display_definition(" + qualified + "*) { return " +
                  description->display_definition + "{}; }";
    if (trait != Trait::Clone) source += " }";
    return context.parse(source);
}
} // namespace rstd::macro
