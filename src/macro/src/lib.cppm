export module rstd.macro;

import std;
import pmacro;

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

auto string_literal(std::string_view spelling) -> std::expected<std::string, std::string> {
    if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"' ||
        spelling.find('\\') != std::string_view::npos)
        return std::unexpected("expected one unescaped ordinary narrow string literal; "
                               "prefixes, raw strings and concatenation are not supported");
    return std::string(spelling.substr(1, spelling.size() - 2));
}

enum class Trait
{
    Debug,
    Display,
    FromStr
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
    case Trait::Debug: return "rstd::Debug";
    case Trait::Display: return "rstd::Display";
    case Trait::FromStr: return "rstd::FromStr";
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
    case Trait::Debug:
        return ((target == Target::Record || target == Target::Enum || target == Target::Field ||
                 target == Target::Enumerator) &&
                option == "rename") ||
               (target == Target::Field && option == "skip");
    case Trait::Display:
        return (target == Target::Record && (option == "format" || option == "transparent")) ||
               (target == Target::Enumerator && option == "rename");
    case Trait::FromStr:
        return (target == Target::Record && option == "transparent") ||
               (target == Target::Enum && option == "ascii_case_insensitive") ||
               (target == Target::Enumerator && (option == "rename" || option == "alias"));
    }
    std::unreachable();
}

struct Options {
    std::optional<std::string> rename;
    std::optional<std::string> format;
    std::vector<std::string>   aliases;
    bool                       skip {}, transparent {}, ascii_case_insensitive {};
};

auto options(pmacro::Context&                       context,
             const std::vector<pmacro::HelperInfo>& helpers,
             Trait                                  trait,
             Target                                 target) -> Checked<Options> {
    Options               result;
    std::set<std::string> seen;
    for (const auto& helper : helpers) {
        if (helper.owner != owner(trait)) continue;
        const auto fail = [&](std::string message) -> Checked<Options> {
            return std::unexpected(
                Error { helper.source, std::string(owner(trait)) + ": " + message });
        };
        if (! helper.arguments) return fail("helper arguments are unavailable");
        auto stream = context.tokens(*helper.arguments);
        if (! stream) return fail("helper tokens are unavailable");
        if (stream->size() == 0) return fail("helper requires at least one option");
        std::size_t i {};
        while (i < stream->size()) {
            const auto token = (*stream)[i++];
            if (token.kind() != pmacro::TokenKind::Ident) return fail("expected a helper option");
            const auto name = std::string(token.spelling());
            if (! allowed(trait, target, name))
                return fail("unknown or invalid option '" + name + "' on " +
                            std::string(target_name(target)));
            if (name != "alias" && ! seen.insert(name).second)
                return fail("duplicate option '" + name + "'");
            if (name == "rename" || name == "alias" || name == "format") {
                if (i == stream->size()) return fail("option '" + name + "' requires a string");
                const auto group = (*stream)[i++];
                if (group.kind() != pmacro::TokenKind::Group ||
                    group.delimiter() != pmacro::Delimiter::Parenthesis)
                    return fail("option '" + name + "' requires parentheses");
                const auto arguments = group.children();
                if (arguments.size() != 1 || arguments[0].kind() != pmacro::TokenKind::Literal)
                    return fail("option '" + name + "' requires one string literal");
                auto text = string_literal(arguments[0].spelling());
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
    return result;
}

struct Field {
    const pmacro::FieldInfo* info;
    std::string              definition;
};

struct MappedFormat {
    std::string              text;
    std::vector<std::size_t> fields;
};

auto map_format(std::string_view format, const std::vector<Field>& fields)
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
        while (index < fields.size() && *fields[index].info->name != name) ++index;
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
};

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
    if (trait == Trait::Display && ! config.format && ! config.transparent)
        return fail(declaration.source, "record requires format(\"...\") or transparent");
    if (trait == Trait::FromStr && ! config.transparent)
        return fail(declaration.source, "record requires explicit transparent");
    if (config.transparent && record.fields.size() != 1)
        return fail(declaration.source, "transparent requires exactly one field");

    std::vector<Field>    fields;
    std::set<std::string> names;
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
        if (field.access != pmacro::Access::Public)
            return fail(field.source, "non-skipped fields must be public");
        const auto name = settings->rename.value_or(*field.name);
        if (! names.insert(name).second)
            return fail(field.source, "duplicate field name '" + name + "'");
        fields.push_back(Field { &field,
                                 "::rstd::derive::Field<" + quote(name) + ",[](const " + qualified +
                                     "& v)->decltype(auto){return (v." + *field.name + ");}>" });
    }
    for (const auto& method : record.methods) {
        auto settings = options(context, method.helpers, trait, Target::Method);
        if (! settings) return std::unexpected(settings.error());
    }

    Description result;
    if (trait == Trait::Debug) {
        result.definition = "::rstd::derive::DebugRecord<" + qualified + ',' +
                            quote(config.rename.value_or(*declaration.name));
    } else if (config.transparent) {
        result.definition = trait == Trait::Display ? "::rstd::derive::TransparentDisplay<"
                                                    : "::rstd::derive::TransparentFromStr<";
        result.definition += qualified + ',' + fields[0].definition + '>';
        if (trait == Trait::FromStr)
            result.assertions = "static_assert(!__is_reference(decltype(" + qualified +
                                "::" + *fields[0].info->name +
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
    if (trait == Trait::FromStr)
        result.definition = "::rstd::derive::EnumFromStr<" + qualified + ',' +
                            (config.ascii_case_insensitive ? "true" : "false");
    else
        result.definition = "::rstd::derive::EnumFormat<" + qualified + ',' +
                            quote(config.rename.value_or(*declaration.name));
    std::set<std::string> names;
    for (const auto& item : enumeration.enumerators) {
        auto settings = options(context, item.helpers, trait, Target::Enumerator);
        if (! settings) return std::unexpected(settings.error());
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
    const auto* record      = std::get_if<pmacro::RecordInfo>(&declaration.data);
    const auto* enumeration = std::get_if<pmacro::EnumInfo>(&declaration.data);
    if (! record && ! enumeration)
        return fail(declaration.source, "derive requires a record or enum");
    auto config =
        options(context, declaration.helpers, trait, record ? Target::Record : Target::Enum);
    if (! config) return failure(context, input.item_span, config.error());
    const auto qualified = "::" + *declaration.qualified_name;
    auto       description =
        record ? record_description(context, declaration, *record, trait, *config, qualified)
               : enum_description(context, declaration, *enumeration, trait, *config, qualified);
    if (! description) return failure(context, input.item_span, description.error());
    const auto  function = trait == Trait::Debug     ? "rstd_debug_definition"
                           : trait == Trait::Display ? "rstd_display_definition"
                                                     : "rstd_from_str_definition";
    std::string source;
    if (declaration.after_item_export == pmacro::AfterItemExport::Required) source += "export ";
    source += "extern \"C++\" { constexpr auto ";
    source += function;
    source += '(' + qualified + "*) {" + description->assertions + "return " +
              description->definition + "{}; } }";
    return context.parse(source);
}

[[pmacro::define("rstd")]]
auto Debug(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::Debug);
}

[[pmacro::define("rstd")]]
auto Display(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::Display);
}

[[pmacro::define("rstd")]]
auto FromStr(pmacro::DeriveInput input, pmacro::Context& context) noexcept -> Output {
    return derive(input, context, Trait::FromStr);
}
