export module rstd.macro:enum_syntax;

import std;
import pmacro;
import :derive;

export namespace rstd::macro
{
struct EnumAttribute {
    std::string         name;
    pmacro::TokenStream arguments;
    pmacro::Span        span;
};
struct EnumField {
    std::string                name;
    bool                       named = false;
    pmacro::TokenStream        type;
    pmacro::TokenStream        initializer;
    std::vector<EnumAttribute> attributes;
    pmacro::Span               span;
};
enum class EnumShape
{
    Unit,
    Tuple,
    Record,
    External
};
struct EnumVariant {
    std::string                name;
    EnumShape                  shape = EnumShape::Unit;
    std::vector<EnumField>     fields;
    std::vector<EnumAttribute> attributes;
    pmacro::TokenStream        external;
    pmacro::Span               span;
};
struct EnumDeclaration {
    std::string                name;
    std::vector<std::string>   parameters;
    std::vector<std::string>   derives;
    std::vector<EnumAttribute> attributes;
    std::vector<EnumVariant>   variants;
    pmacro::TokenStream        members;
    pmacro::Span               span;
};

auto enum_punct(pmacro::TokenTree token, std::string_view spelling) -> bool {
    return token.kind() == pmacro::TokenKind::Punct && token.spelling() == spelling;
}
auto enum_group(pmacro::TokenTree token, pmacro::Delimiter delimiter) -> bool {
    return token.kind() == pmacro::TokenKind::Group && token.delimiter() == delimiter;
}
auto enum_ident(pmacro::TokenTree token, std::string_view name) -> bool {
    return token.kind() == pmacro::TokenKind::Ident && token.spelling() == name;
}
auto enum_source(pmacro::Span span) -> pmacro::SourceInfo {
    pmacro::SourceInfo source;
    source.diagnostic = span;
    return source;
}
auto enum_fail(pmacro::Span span, std::string message) -> std::unexpected<Error> {
    return std::unexpected(Error { enum_source(span), "rstd::Enum: " + std::move(message) });
}
auto enum_slice(pmacro::Context&    context,
                pmacro::TokenStream tokens,
                std::size_t         begin,
                std::size_t         end) -> pmacro::TokenStream {
    auto result = context.stream();
    if (! result) return {};
    for (auto index = begin; index < end; ++index)
        if (! result->append(tokens[index])) return {};
    return *result;
}
auto enum_attributes(pmacro::Context& context, pmacro::TokenStream tokens, std::size_t& index)
    -> Checked<std::vector<EnumAttribute>> {
    std::vector<EnumAttribute> result;
    while (index < tokens.size() && enum_group(tokens[index], pmacro::Delimiter::Bracket)) {
        const auto outer = tokens[index++];
        const auto body  = outer.children();
        if (body.size() != 1 || ! enum_group(body[0], pmacro::Delimiter::Bracket))
            return enum_fail(outer.span(), "attributes require [[...]]");
        const auto attributes = body[0].children();
        for (std::size_t i = 0; i < attributes.size();) {
            auto token = attributes[i++];
            if (token.kind() != pmacro::TokenKind::Ident)
                return enum_fail(token.span(), "expected an attribute name");
            EnumAttribute attribute { std::string(token.spelling()), {}, token.span() };
            while (i < attributes.size() && enum_punct(attributes[i], "::")) {
                attribute.name += "::";
                if (++i == attributes.size() || attributes[i].kind() != pmacro::TokenKind::Ident)
                    return enum_fail(token.span(), "expected a qualified attribute name");
                attribute.name += attributes[i++].spelling();
            }
            if (i < attributes.size() && enum_group(attributes[i], pmacro::Delimiter::Parenthesis))
                attribute.arguments = attributes[i++].children();
            result.push_back(std::move(attribute));
            if (i < attributes.size()) {
                if (! enum_punct(attributes[i], ","))
                    return enum_fail(attributes[i].span(), "expected ',' between attributes");
                ++i;
            }
        }
    }
    return result;
}
auto enum_qualified(pmacro::TokenStream tokens, std::size_t& index) -> Checked<std::string> {
    if (index == tokens.size()) return enum_fail({}, "expected a derive name");
    auto first = tokens[index++];
    if (first.kind() == pmacro::TokenKind::Literal) {
        auto text = string_literal(first.spelling());
        if (! text || text->empty()) return enum_fail(first.span(), "expected a derive name");
        if (text->find("::") == std::string::npos) return "rstd::" + *text;
        return *text;
    }
    if (first.kind() != pmacro::TokenKind::Ident)
        return enum_fail(first.span(), "expected a derive name");
    std::string result(first.spelling());
    while (index < tokens.size() && enum_punct(tokens[index], "::")) {
        result += "::";
        if (++index == tokens.size() || tokens[index].kind() != pmacro::TokenKind::Ident)
            return enum_fail(first.span(), "expected a qualified derive name");
        result += tokens[index++].spelling();
    }
    if (result.find("::") == std::string::npos) result = "rstd::" + result;
    return result;
}
auto enum_fields(pmacro::Context& context, pmacro::TokenTree body, EnumShape shape)
    -> Checked<std::vector<EnumField>> {
    const auto             tokens = body.children();
    std::vector<EnumField> result;
    std::set<std::string>  names;
    for (std::size_t index = 0; index < tokens.size();) {
        auto attributes = enum_attributes(context, tokens, index);
        if (! attributes) return std::unexpected(attributes.error());
        if (index == tokens.size())
            return enum_fail(body.span(), "expected a field after attributes");
        EnumField field;
        field.span       = tokens[index].span();
        field.attributes = std::move(*attributes);
        field.named      = index + 1 < tokens.size() &&
                           tokens[index].kind() == pmacro::TokenKind::Ident &&
                           enum_punct(tokens[index + 1], ":");
        if (shape == EnumShape::Record && ! field.named)
            return enum_fail(field.span, "record field requires 'name: Type'");
        if (field.named) {
            field.name = std::string(tokens[index].spelling());
            index += 2;
        } else
            field.name = "field_" + std::to_string(result.size());
        if (field.name == "Self" || field.name == "get" || field.name.starts_with("rstd_enum_"))
            return enum_fail(field.span, "reserved field name '" + field.name + "'");
        if (! names.insert(field.name).second)
            return enum_fail(field.span, "duplicate field '" + field.name + "'");
        const auto                 begin = index;
        std::optional<std::size_t> initializer;
        int                        angle = 0;
        while (index < tokens.size()) {
            const auto token = tokens[index];
            if (enum_punct(token, ";"))
                return enum_fail(token.span(), "field type cannot contain a declaration delimiter");
            if (angle == 0 && enum_punct(token, ",")) break;
            if (angle == 0 && enum_punct(token, "=")) {
                initializer = index++;
                break;
            }
            if (enum_punct(token, "<")) ++angle;
            if (enum_punct(token, ">")) --angle;
            if (enum_punct(token, ">>")) angle -= 2;
            if (angle < 0)
                return enum_fail(token.span(), "unbalanced template arguments in field type");
            ++index;
        }
        if (angle != 0) return enum_fail(field.span, "unbalanced template arguments in field type");
        const auto type_end = initializer.value_or(index);
        if (begin == type_end) return enum_fail(field.span, "expected a field type");
        field.type = enum_slice(context, tokens, begin, type_end);
        if (initializer) {
            const auto start = index;
            while (index < tokens.size() && ! enum_punct(tokens[index], ",")) {
                if (enum_punct(tokens[index], ";"))
                    return enum_fail(tokens[index].span(),
                                     "field initializer cannot contain a declaration delimiter");
                ++index;
            }
            if (index == start) return enum_fail(field.span, "expected a field initializer");
            field.initializer = enum_slice(context, tokens, start, index);
        }
        result.push_back(std::move(field));
        if (index < tokens.size()) ++index;
    }
    return result;
}
auto parse_enum(pmacro::FunctionInput input, pmacro::Context& context) -> Checked<EnumDeclaration> {
    if (input.scope != pmacro::DeclarationScope::Namespace)
        return enum_fail(input.invocation_span, "enum declarations require namespace scope");
    const auto      tokens = input.arguments;
    EnumDeclaration result;
    result.span       = input.invocation_span;
    std::size_t index = 0;
    if (index < tokens.size() && enum_ident(tokens[index], "template")) {
        const auto at = tokens[index++].span();
        if (index == tokens.size() || ! enum_punct(tokens[index++], "<"))
            return enum_fail(at, "expected '<' after template");
        std::set<std::string> parameters;
        while (index < tokens.size() && ! enum_punct(tokens[index], ">")) {
            auto parameter = tokens[index++];
            if (! enum_ident(parameter, "class") && ! enum_ident(parameter, "typename"))
                return enum_fail(parameter.span(),
                                 "only named type template parameters are supported");
            if (index == tokens.size() || tokens[index].kind() != pmacro::TokenKind::Ident)
                return enum_fail(parameter.span(), "template parameters must be named types");
            auto name = std::string(tokens[index++].spelling());
            if (! parameters.insert(name).second)
                return enum_fail(parameter.span(), "duplicate template parameter '" + name + "'");
            result.parameters.push_back(std::move(name));
            if (index < tokens.size() && enum_punct(tokens[index], ">")) break;
            if (index == tokens.size() || ! enum_punct(tokens[index++], ","))
                return enum_fail(parameter.span(),
                                 "only named type template parameters are supported");
        }
        if (result.parameters.empty() || index == tokens.size())
            return enum_fail(at, "expected named type template parameters");
        ++index;
    }
    auto attributes = enum_attributes(context, tokens, index);
    if (! attributes) return std::unexpected(attributes.error());
    std::set<std::string> derives;
    for (auto& attribute : *attributes) {
        if (attribute.name != "derive") {
            result.attributes.push_back(std::move(attribute));
            continue;
        }
        if (! attribute.arguments.valid() || attribute.arguments.size() == 0)
            return enum_fail(attribute.span, "derive requires trait names");
        for (std::size_t i = 0; i < attribute.arguments.size();) {
            auto trait = enum_qualified(attribute.arguments, i);
            if (! trait) return std::unexpected(trait.error());
            if (! derives.insert(*trait).second)
                return enum_fail(attribute.span, "duplicate derive '" + *trait + "'");
            result.derives.push_back(std::move(*trait));
            if (i < attribute.arguments.size()) {
                if (! enum_punct(attribute.arguments[i], ","))
                    return enum_fail(attribute.arguments[i].span(), "expected ',' between derives");
                ++i;
            }
        }
    }
    if (index == tokens.size() || ! enum_ident(tokens[index++], "enum"))
        return enum_fail(input.invocation_span, "expected an enum declaration");
    if (index == tokens.size() || tokens[index].kind() != pmacro::TokenKind::Ident)
        return enum_fail(input.invocation_span, "expected an enum name");
    result.name = std::string(tokens[index++].spelling());
    const std::set<std::string> public_names { "Self", "Tag",   "Variants", "index",
                                               "tag",  "visit", "visit_mut" };
    if (public_names.contains(result.name) || result.name.starts_with("rstd_enum_"))
        return enum_fail(result.span, "reserved enum name '" + result.name + "'");
    if (index == tokens.size() || ! enum_group(tokens[index], pmacro::Delimiter::Brace))
        return enum_fail(input.invocation_span, "expected a variant block");
    const auto                  body = tokens[index++].children();
    const std::set<std::string> reserved { result.name, "Self", "Tag",   "Variants",
                                           "index",     "tag",  "visit", "visit_mut" };
    std::set<std::string>       names;
    for (std::size_t i = 0; i < body.size();) {
        auto variant_attributes = enum_attributes(context, body, i);
        if (! variant_attributes) return std::unexpected(variant_attributes.error());
        if (i == body.size() || body[i].kind() != pmacro::TokenKind::Ident)
            return enum_fail(input.invocation_span, "expected a variant name");
        EnumVariant variant;
        variant.name       = std::string(body[i].spelling());
        variant.span       = body[i++].span();
        variant.attributes = std::move(*variant_attributes);
        if (reserved.contains(variant.name) || variant.name.starts_with("rstd_enum_"))
            return enum_fail(variant.span, "reserved variant name '" + variant.name + "'");
        if (! names.insert(variant.name).second)
            return enum_fail(variant.span, "duplicate variant '" + variant.name + "'");
        if (i < body.size() && (enum_group(body[i], pmacro::Delimiter::Parenthesis) ||
                                enum_group(body[i], pmacro::Delimiter::Brace))) {
            variant.shape = enum_group(body[i], pmacro::Delimiter::Parenthesis) ? EnumShape::Tuple
                                                                                : EnumShape::Record;
            auto fields   = enum_fields(context, body[i++], variant.shape);
            if (! fields) return std::unexpected(fields.error());
            variant.fields = std::move(*fields);
        } else if (i < body.size() && enum_punct(body[i], "=")) {
            const auto start = ++i;
            int        angle = 0;
            while (i < body.size()) {
                if (enum_punct(body[i], ";"))
                    return enum_fail(
                        body[i].span(),
                        "external payload type cannot contain a declaration delimiter");
                if (angle == 0 && enum_punct(body[i], ",")) break;
                if (enum_punct(body[i], "<")) ++angle;
                if (enum_punct(body[i], ">")) --angle;
                if (enum_punct(body[i], ">>")) angle -= 2;
                if (angle < 0) return enum_fail(body[i].span(), "unbalanced external payload type");
                ++i;
            }
            if (start == i || angle != 0)
                return enum_fail(variant.span, "expected an external payload type");
            variant.external = enum_slice(context, body, start, i);
            variant.shape    = EnumShape::External;
        }
        result.variants.push_back(std::move(variant));
        if (i < body.size()) {
            if (! enum_punct(body[i], ","))
                return enum_fail(body[i].span(), "expected ',' between variants");
            ++i;
        }
    }
    if (result.variants.empty())
        return enum_fail(result.span, "enum requires at least one variant");
    auto generated_names = reserved;
    for (const auto& variant : result.variants) {
        std::vector<std::string> symbols {
            variant.name, "is_" + variant.name, "replace_" + variant.name, variant.name + "_variant"
        };
        if (variant.shape != EnumShape::Unit) {
            symbols.push_back("as_" + variant.name);
            symbols.push_back(variant.name + "_payload");
        }
        for (const auto& name : symbols)
            if (! generated_names.insert(name).second)
                return enum_fail(variant.span, "generated member name collision '" + name + "'");
    }
    const std::set<std::string> descriptor_names { "payload_type",
                                                   "kind",
                                                   "name",
                                                   "matches",
                                                   "make",
                                                   "get",
                                                   "rstd_payload_definition",
                                                   "rstd_debug_definition",
                                                   "rstd_clone_definition",
                                                   "rstd_error_definition",
                                                   "rstd_display_definition" };
    for (const auto& parameter : result.parameters)
        if (generated_names.contains(parameter) || descriptor_names.contains(parameter) ||
            parameter.starts_with("rstd_enum_"))
            return enum_fail(result.span, "reserved template parameter '" + parameter + "'");
    if (index < tokens.size() && enum_ident(tokens[index], "members")) {
        const auto at = tokens[index++].span();
        if (index == tokens.size() || ! enum_group(tokens[index], pmacro::Delimiter::Brace))
            return enum_fail(at, "expected a members block");
        result.members = tokens[index++].children();
        for (auto token : result.members)
            if (enum_group(token, pmacro::Delimiter::Brace))
                return enum_fail(token.span(),
                                 "members accepts declarations, not inline definitions");
    }
    if (index < tokens.size() && enum_punct(tokens[index], ";")) ++index;
    if (index < tokens.size() && enum_punct(tokens[index], ",")) ++index;
    if (index != tokens.size()) return enum_fail(tokens[index].span(), "unexpected enum input");
    return result;
}
} // namespace rstd::macro
