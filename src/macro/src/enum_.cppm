export module rstd.macro:enum_;

import std;
import pmacro;
import :derive;
import :enum_syntax;

export namespace rstd::macro
{
struct EnumOutput {
    std::map<std::string, pmacro::TokenStream> splices;

    auto insert(pmacro::TokenStream tokens) -> std::string {
        auto name = "rstd_enum_splice_" + std::to_string(splices.size());
        splices.emplace(name, tokens);
        return name;
    }
    auto compose(pmacro::TokenStream tokens, pmacro::Context& context) const -> Output {
        auto output = context.stream();
        if (! output) return std::unexpected(output.error());
        for (auto token : tokens) {
            if (token.kind() == pmacro::TokenKind::Ident) {
                auto found = splices.find(std::string(token.spelling()));
                if (found != splices.end()) {
                    if (! output->append(found->second))
                        return std::unexpected(pmacro::Error::Failure);
                    continue;
                }
            }
            if (token.kind() == pmacro::TokenKind::Group) {
                auto children = compose(token.children(), context);
                if (! children) return std::unexpected(children.error());
                auto group = context.group(token.delimiter(), *children, token.span());
                if (! group) return std::unexpected(group.error());
                if (! output->append(*group)) return std::unexpected(pmacro::Error::Failure);
            } else if (! output->append(token))
                return std::unexpected(pmacro::Error::Failure);
        }
        return *output;
    }
};
struct EnumHelper {
    std::string         owner;
    pmacro::TokenStream arguments;
    pmacro::Span        span;
};
auto enum_helper(const EnumAttribute& attribute, pmacro::Context& context) -> Checked<EnumHelper> {
    auto args = attribute.arguments;
    if (! args.valid() || args.size() < 3 || args[0].kind() != pmacro::TokenKind::Literal ||
        ! enum_punct(args[1], ","))
        return enum_fail(attribute.span, "helper requires an owner and options");
    auto name = string_literal(args[0].spelling());
    if (! name || name->empty()) return enum_fail(attribute.span, "helper requires an owner name");
    return EnumHelper { *name, enum_slice(context, args, 2, args.size()), attribute.span };
}
auto enum_helpers(const std::vector<EnumAttribute>& attributes, pmacro::Context& context)
    -> Checked<std::vector<EnumHelper>> {
    std::vector<EnumHelper> result;
    for (const auto& attribute : attributes) {
        if (attribute.name != "helper") continue;
        auto parsed = enum_helper(attribute, context);
        if (! parsed) return std::unexpected(parsed.error());
        result.push_back(std::move(*parsed));
    }
    return result;
}
auto enum_has_derive(const EnumDeclaration& declaration, std::string_view name) -> bool {
    return std::ranges::find(declaration.derives, name) != declaration.derives.end();
}
auto enum_foreign(const EnumDeclaration& declaration) -> std::vector<std::string> {
    std::vector<std::string> result;
    for (const auto& trait : declaration.derives)
        if (! trait.starts_with("rstd::")) result.push_back(trait);
    return result;
}
auto enum_template(const EnumDeclaration& declaration) -> std::string {
    if (declaration.parameters.empty()) return {};
    std::string result = "template<";
    for (const auto& parameter : declaration.parameters) {
        if (result.back() != '<') result += ',';
        result += "class " + parameter;
    }
    return result + '>';
}
auto enum_arguments(const EnumDeclaration& declaration) -> std::string {
    if (declaration.parameters.empty()) return {};
    std::string result = "<";
    for (const auto& parameter : declaration.parameters) {
        if (result.back() != '<') result += ',';
        result += parameter;
    }
    return result + '>';
}
auto enum_payload_name(const EnumDeclaration& declaration, const EnumVariant& variant)
    -> std::string {
    return "rstd_enum_" + std::to_string(declaration.name.size()) + '_' + declaration.name + '_' +
           variant.name;
}
auto enum_type(const EnumDeclaration& declaration) -> std::string {
    return declaration.name + enum_arguments(declaration);
}
auto enum_payload_type(const EnumDeclaration& declaration, const EnumVariant& variant)
    -> std::string {
    return enum_payload_name(declaration, variant) + enum_arguments(declaration);
}
auto enum_helper_arguments(const std::vector<EnumHelper>& helpers,
                           std::string_view               owner,
                           EnumOutput&                    output) -> std::string {
    std::string result;
    for (const auto& helper : helpers) {
        if (helper.owner != owner) continue;
        if (! result.empty()) result += ',';
        result += output.insert(helper.arguments);
    }
    return result;
}
auto enum_owner_options(const std::vector<EnumAttribute>& attributes,
                        Trait                             trait,
                        Target                            target,
                        pmacro::Context&                  context) -> Checked<Options> {
    auto helpers = enum_helpers(attributes, context);
    if (! helpers) return std::unexpected(helpers.error());
    std::vector<HelperTokens> inputs;
    for (const auto& helper : *helpers)
        if (helper.owner == owner(trait))
            inputs.push_back({ helper.arguments, enum_source(helper.span) });
    for (const auto& attribute : attributes) {
        std::string code;
        if (trait == Trait::Error) {
            if (attribute.name == "error") {
                const auto args = attribute.arguments;
                if (! args.valid() || args.size() != 1)
                    return enum_fail(attribute.span,
                                     "error requires one format string or transparent");
                if (enum_ident(args[0], "transparent"))
                    code = "transparent";
                else if (args[0].kind() == pmacro::TokenKind::Literal)
                    code = "format(" + std::string(args[0].spelling()) + ')';
                else
                    return enum_fail(attribute.span,
                                     "error requires one format string or transparent");
            } else if (attribute.name == "transparent" || attribute.name == "source" ||
                       attribute.name == "from")
                code = attribute.name;
        } else if (trait == Trait::Display && attribute.name == "display") {
            if (! attribute.arguments.valid() || attribute.arguments.size() != 1 ||
                attribute.arguments[0].kind() != pmacro::TokenKind::Literal)
                return enum_fail(attribute.span, "display requires one format string");
            code = "format(" + std::string(attribute.arguments[0].spelling()) + ')';
        } else if (trait == Trait::Debug && attribute.name == "debug") {
            if (! attribute.arguments.valid())
                return enum_fail(attribute.span, "debug requires options");
            inputs.push_back({ attribute.arguments, enum_source(attribute.span) });
        }
        if (code.empty()) continue;
        auto parsed = context.parse(code);
        if (! parsed) return enum_fail(attribute.span, "cannot read attribute options");
        inputs.push_back({ *parsed, enum_source(attribute.span) });
    }
    auto parsed = options(inputs, trait, target);
    if (! parsed) return std::unexpected(parsed.error());
    if (parsed->variants || ! parsed->units.empty())
        return enum_fail(attributes.empty() ? pmacro::Span {} : attributes.front().span,
                         "declare variants and unit formats in the enum body");
    return parsed;
}
auto enum_validate_attributes(const EnumDeclaration&            declaration,
                              const std::vector<EnumAttribute>& attributes,
                              std::string_view                  scope,
                              pmacro::Context&                  context) -> Checked<bool> {
    for (const auto& attribute : attributes) {
        if (attribute.name == "helper") {
            auto helper = enum_helper(attribute, context);
            if (! helper) return std::unexpected(helper.error());
            if (! enum_has_derive(declaration, helper->owner))
                return enum_fail(attribute.span,
                                 "helper owner '" + helper->owner + "' is not derived");
            if (helper->owner.starts_with("rstd::") &&
                (scope == "type" || helper->owner == "rstd::Clone" ||
                 (scope == "field" && helper->owner != "rstd::Debug" &&
                  helper->owner != "rstd::Error")))
                return enum_fail(attribute.span,
                                 "this derive does not accept " + std::string(scope) +
                                     " helper options");
            continue;
        }
        const bool error   = enum_has_derive(declaration, "rstd::Error");
        const bool debug   = enum_has_derive(declaration, "rstd::Debug");
        const bool display = enum_has_derive(declaration, "rstd::Display");
        if (scope == "variant" && attribute.name == "default") {
            if (attribute.arguments.valid())
                return enum_fail(attribute.span, "default does not accept arguments");
            continue;
        }
        if (scope == "variant" && error &&
            (attribute.name == "error" || attribute.name == "transparent")) {
            if (attribute.name == "transparent" && attribute.arguments.valid())
                return enum_fail(attribute.span, "transparent does not accept arguments");
            continue;
        }
        if (scope == "variant" && display && attribute.name == "display") continue;
        if (scope == "field" && error && (attribute.name == "source" || attribute.name == "from")) {
            if (attribute.arguments.valid())
                return enum_fail(attribute.span, attribute.name + " does not accept arguments");
            continue;
        }
        if (scope != "type" && debug && attribute.name == "debug") continue;
        return enum_fail(attribute.span,
                         "unknown or invalid " + std::string(scope) + " attribute '" +
                             attribute.name + "'");
    }
    return true;
}
auto enum_foreign_attributes(const std::vector<EnumHelper>&  helpers,
                             const std::vector<std::string>& derives,
                             EnumOutput&                     output) -> std::string {
    std::string result;
    for (const auto& helper : helpers) {
        if (std::ranges::find(derives, helper.owner) == derives.end()) continue;
        result += "[[pmacro::helper(" + quote(helper.owner) + ',' +
                  output.insert(helper.arguments) + ")]]";
    }
    return result;
}
auto enum_payload_metadata(const EnumDeclaration& declaration, const EnumVariant& variant)
    -> std::string {
    const auto  name       = enum_payload_name(declaration, variant);
    std::string definition = "::rstd::VariantPayload<" + name + ",::rstd::VariantKind::" +
                             (variant.shape == EnumShape::Tuple ? "Tuple" : "Record") + ',' +
                             quote(variant.name);
    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
        const auto& field = variant.fields[i];
        definition += ",::rstd::PayloadField<" +
                      quote(field.named ? field.name : std::to_string(i)) + ",&" + name +
                      "::" + field.name + '>';
    }
    definition += '>';
    return "friend constexpr auto rstd_payload_definition(" + name + "*){return " + definition +
           "{};}";
}
auto enum_getters(const EnumVariant& variant) -> std::string {
    std::string result;
    for (const auto& [ref, cast] : std::array<std::pair<std::string_view, std::string_view>, 4> {
             { { "&", "" },
               { "const&", "" },
               { "&&", "::rstd::move" },
               { "const&&", "::rstd::move" } } }) {
        result += "template<::rstd::size_t rstd_enum_index> constexpr decltype(auto) get() " +
                  std::string(ref) + " noexcept { static_assert(rstd_enum_index<" +
                  std::to_string(variant.fields.size()) +
                  ",\"tuple payload index is out of range\");";
        for (std::size_t i = 0; i < variant.fields.size(); ++i) {
            if (i != 0) result += "else ";
            result += "if constexpr(rstd_enum_index==" + std::to_string(i) + ") return ";
            if (cast.empty())
                result += "(this->" + variant.fields[i].name + ')';
            else
                result += std::string(cast) + "(this->" + variant.fields[i].name + ')';
            result += ';';
        }
        result += '}';
    }
    return result;
}
auto enum_emit_payload(const EnumDeclaration&         declaration,
                       const EnumVariant&             variant,
                       const std::vector<EnumHelper>& root_helpers,
                       EnumOutput&                    output,
                       pmacro::Context&               context) -> Checked<std::string> {
    if (variant.shape == EnumShape::Unit || variant.shape == EnumShape::External)
        return std::string();
    const auto foreign = enum_foreign(declaration);
    auto       helpers = enum_helpers(variant.attributes, context);
    if (! helpers) return std::unexpected(helpers.error());
    std::string result = enum_template(declaration) + "struct ";
    for (const auto& derive : foreign) {
        result += "[[pmacro::derive(" + quote(derive) + "),pmacro::helper(" + quote(derive) +
                  ",described_payload,name(" + quote(variant.name) + ')';
        auto parent = enum_helper_arguments(root_helpers, derive, output);
        auto own    = enum_helper_arguments(*helpers, derive, output);
        if (! parent.empty()) result += ",parent(" + parent + ')';
        if (! own.empty()) result += ",variant(" + own + ')';
        result += ")]]";
    }
    const auto name = enum_payload_name(declaration, variant);
    result += name + "{using Self=" + enum_type(declaration) + ';';
    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
        const auto& field         = variant.fields[i];
        auto        field_helpers = enum_helpers(field.attributes, context);
        if (! field_helpers) return std::unexpected(field_helpers.error());
        const auto type = "rstd_enum_field_" + std::to_string(i) + "_type";
        result += "using " + type + '=' + output.insert(field.type) + ';';
        result += enum_foreign_attributes(*field_helpers, foreign, output);
        result += type + ' ' + field.name;
        if (field.initializer.valid()) result += '=' + output.insert(field.initializer);
        result += ';';
    }
    if (variant.shape == EnumShape::Tuple && ! variant.fields.empty())
        result += enum_getters(variant);
    result += enum_payload_metadata(declaration, variant) + "};";
    return result;
}
auto enum_factory(const EnumVariant& variant) -> std::string {
    const auto& name = variant.name;
    if (variant.shape == EnumShape::Unit)
        return "[[nodiscard]]static constexpr auto " + name +
               "() noexcept->Self{return Self(rstd_enum_in_place_t<Tag::" + name + ">{});}";
    return "template<class...rstd_enum_args> requires ::rstd::mtp::init<" + name +
           "_payload,rstd_enum_args...> [[nodiscard]]static constexpr auto " + name +
           "(rstd_enum_args&&...rstd_enum_values) noexcept(::rstd::mtp::noex_init_v<" + name +
           "_payload,rstd_enum_args...>)->Self{return Self(rstd_enum_in_place_t<Tag::" + name +
           ">{},::rstd::forward<rstd_enum_args>(rstd_enum_values)...);}";
}
auto enum_accessors(const EnumVariant& variant) -> std::string {
    const auto& name = variant.name;
    std::string result =
        "[[nodiscard]]constexpr auto is_" + name +
        "()const noexcept->bool{return rstd_enum_choice_.template is<Tag::" + name + ">();}";
    if (variant.shape == EnumShape::Unit)
        return result + "constexpr void replace_" + name +
               "()noexcept{rstd_enum_choice_.template set<Tag::" + name + ">();}";
    result += "template<class...rstd_enum_args> requires requires(rstd_enum_choice_type& "
              "rstd_enum_choice,rstd_enum_args&&...rstd_enum_values){rstd_enum_choice.template "
              "set<Tag::" +
              name +
              ">(::rstd::forward<rstd_enum_args>(rstd_enum_values)...);}constexpr void replace_" +
              name +
              "(rstd_enum_args&&...rstd_enum_values)noexcept(noexcept(rstd_enum_choice_.template "
              "set<Tag::" +
              name +
              ">(::rstd::forward<rstd_enum_args>(rstd_enum_values)...))){rstd_enum_choice_."
              "template set<Tag::" +
              name + ">(::rstd::forward<rstd_enum_args>(rstd_enum_values)...);}";
    for (const auto& [ref, cast] : std::array<std::pair<std::string_view, std::string_view>, 4> {
             { { "&", "" },
               { "const&", "" },
               { "&&", "::rstd::move" },
               { "const&&", "::rstd::move" } } }) {
        result += "[[nodiscard]]constexpr decltype(auto) as_" + name + "()" + std::string(ref) +
                  " noexcept{return ";
        if (cast.empty())
            result += "rstd_enum_choice_";
        else
            result += std::string(cast) + "(rstd_enum_choice_)";
        result += ".template as<Tag::" + name + ">();}";
    }
    return result;
}
auto enum_visitors() -> std::string {
    std::string result;
    for (const auto& method : { "visit", "visit_mut" }) {
        for (const auto& [ref, cast, type] : std::array<std::array<std::string_view, 3>, 4> {
                 { { { "&", "", "rstd_enum_choice_type&" } },
                   { { "const&", "", "const rstd_enum_choice_type&" } },
                   { { "&&", "::rstd::move", "rstd_enum_choice_type&&" } },
                   { { "const&&", "::rstd::move", "const rstd_enum_choice_type&&" } } } }) {
            if (std::string_view(method) == "visit_mut" && ref.starts_with("const")) continue;
            const auto invoke =
                cast.empty() ? "rstd_enum_choice_" : std::string(cast) + "(rstd_enum_choice_)";
            const auto expression =
                invoke + '.' + method + "(::rstd::forward<rstd_enum_function>(rstd_enum_callable))";
            result +=
                "template<class rstd_enum_function> requires requires(" + std::string(type) +
                " rstd_enum_choice,rstd_enum_function&&rstd_enum_callable){" +
                (cast.empty() ? "rstd_enum_choice" : std::string(cast) + "(rstd_enum_choice)") +
                '.' + method +
                "(::rstd::forward<rstd_enum_function>(rstd_enum_callable));} constexpr "
                "decltype(auto) " +
                method + "(rstd_enum_function&&rstd_enum_callable)" + std::string(ref) +
                " noexcept(noexcept(" + expression + ")){return " + expression + ";}";
        }
    }
    return result;
}
auto enum_descriptor(const EnumDeclaration& declaration, const EnumVariant& variant)
    -> std::string {
    const auto& name   = variant.name;
    std::string result = "struct " + name + "_variant{using payload_type=" +
                         (variant.shape == EnumShape::Unit ? "void" : name + "_payload") + ';';
    const auto  kind   = variant.shape == EnumShape::Unit     ? "Unit"
                         : variant.shape == EnumShape::Tuple  ? "Tuple"
                         : variant.shape == EnumShape::Record ? "Record"
                                                              : "External";
    result += "static constexpr auto kind=::rstd::VariantKind::" + std::string(kind) + ';';
    result += "static constexpr char name[]=" + quote(name) + ';';
    result += "static constexpr bool matches(const Self& rstd_enum_value)noexcept{return "
              "rstd_enum_value.is_" +
              name + "();}";
    result += "template<class...rstd_enum_args>static constexpr auto "
              "make(rstd_enum_args&&...rstd_enum_values)->Self{return Self::" +
              name + "(::rstd::forward<rstd_enum_args>(rstd_enum_values)...);}";
    if (variant.shape != EnumShape::Unit)
        result += "static constexpr auto get(const Self&rstd_enum_value)->const "
                  "payload_type&{return rstd_enum_value.as_" +
                  name + "();}";
    return result + "};";
}
auto enum_foreign_root(const EnumDeclaration&         declaration,
                       const std::vector<EnumHelper>& helpers,
                       EnumOutput&                    output,
                       pmacro::Context&               context) -> Checked<std::string> {
    std::string result;
    for (const auto& derive : enum_foreign(declaration)) {
        result += "[[pmacro::derive(" + quote(derive) + "),pmacro::helper(" + quote(derive) +
                  ",described_enum";
        auto root = enum_helper_arguments(helpers, derive, output);
        if (! root.empty()) result += ',' + root;
        for (const auto& variant : declaration.variants) {
            auto attrs = enum_helpers(variant.attributes, context);
            if (! attrs) return std::unexpected(attrs.error());
            auto options = enum_helper_arguments(*attrs, derive, output);
            if (! options.empty())
                result += ",variant(" + quote(variant.name) + ',' + options + ')';
        }
        result += ")]]";
    }
    return result;
}
auto enum_emit_class(const EnumDeclaration&         declaration,
                     const std::vector<EnumHelper>& helpers,
                     EnumOutput&                    output,
                     pmacro::Context&               context) -> Checked<std::string> {
    auto attrs = enum_foreign_root(declaration, helpers, output, context);
    if (! attrs) return std::unexpected(attrs.error());
    std::string result = enum_template(declaration) +
                         "class [[pmacro::derive(\"rstd::EnumCheck\")]]" + *attrs +
                         declaration.name + "{public:using Self=" + declaration.name + ';';
    const auto  count  = declaration.variants.size();
    result +=
        "static constexpr ::rstd::size_t rstd_enum_variant_count=" + std::to_string(count) + ';';
    for (const auto& variant : declaration.variants)
        if (variant.shape != EnumShape::Unit)
            result +=
                "using " + variant.name + "_payload=" +
                (variant.shape == EnumShape::External ? output.insert(variant.external)
                                                      : enum_payload_type(declaration, variant)) +
                ';';
    result += "enum class Tag: ";
    result += count <= 0xff     ? "::rstd::uint8_t"
              : count <= 0xffff ? "::rstd::uint16_t"
                                : "::rstd::uint32_t";
    result += '{';
    for (const auto& variant : declaration.variants) result += variant.name + ',';
    result += "};private:using rstd_enum_choice_type=::rstd::Choice<";
    for (std::size_t i = 0; i < count; ++i) {
        const auto& variant = declaration.variants[i];
        if (i != 0) result += ',';
        result += "::rstd::choice_case<Tag::" + variant.name + ',' +
                  (variant.shape == EnumShape::Unit ? "void" : variant.name + "_payload") + '>';
    }
    result += ">;template<Tag rstd_enum_tag>struct rstd_enum_in_place_t{};rstd_enum_choice_type "
              "rstd_enum_choice_;";
    result += "template<Tag rstd_enum_tag,class...rstd_enum_args>explicit constexpr " +
              declaration.name +
              "(rstd_enum_in_place_t<rstd_enum_tag>,rstd_enum_args&&...rstd_enum_values)noexcept("
              "noexcept(rstd_"
              "enum_choice_type::"
              "template with<rstd_enum_tag>(::rstd::forward<rstd_enum_args>(rstd_enum_values)...)))"
              ":rstd_enum_choice_(rstd_enum_choice_type::template "
              "with<rstd_enum_tag>(::rstd::forward<rstd_enum_args>(rstd_enum_values)...)){}public:";
    for (const auto& variant : declaration.variants) result += enum_factory(variant);
    for (const auto& variant : declaration.variants) result += enum_accessors(variant);
    result += enum_visitors();
    for (const auto& variant : declaration.variants)
        result += enum_descriptor(declaration, variant);
    result += "using Variants=::rstd::VariantList<";
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) result += ',';
        result += declaration.variants[i].name + "_variant";
    }
    result +=
        ">;[[nodiscard]]constexpr auto index()const noexcept->::rstd::size_t{return "
        "rstd_enum_choice_.index();}"
        "[[nodiscard]]constexpr auto tag()const noexcept->Tag{return rstd_enum_choice_.which();}";
    const EnumVariant* default_variant = nullptr;
    for (const auto& variant : declaration.variants)
        for (const auto& attribute : variant.attributes)
            if (attribute.name == "default") {
                if (default_variant) return enum_fail(attribute.span, "multiple default variants");
                default_variant = &variant;
            }
    if (default_variant)
        result += "template<class rstd_enum_default_choice=rstd_enum_choice_type>requires "
                  "requires{rstd_enum_default_choice::template with<Tag::" +
                  default_variant->name + ">();}constexpr " + declaration.name +
                  "()noexcept(noexcept(rstd_enum_default_choice::template with<Tag::" +
                  default_variant->name +
                  ">())):rstd_enum_choice_(rstd_enum_default_choice::template with<Tag::" +
                  default_variant->name + ">()){}";
    if (declaration.members.valid()) result += output.insert(declaration.members);
    return result + "};";
}
auto enum_fields_definition(const EnumDeclaration& declaration, const EnumVariant& variant)
    -> std::vector<Field> {
    std::vector<Field> result;
    const auto         payload = enum_payload_type(declaration, variant);
    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
        const auto& field = variant.fields[i];
        result.push_back({ field.named ? field.name : std::to_string(i),
                           "typename ::rstd::PayloadDefinition<" + payload +
                               ">::template field_at<" + std::to_string(i) + '>' });
    }
    return result;
}
auto enum_adl(const EnumDeclaration& declaration,
              std::string_view       type,
              std::string_view       function,
              std::string_view       definition) -> std::string {
    return enum_template(declaration) + "constexpr auto " + std::string(function) + '(' +
           std::string(type) + "*){return " + std::string(definition) + "{};}";
}
auto enum_emit_derives(const EnumDeclaration& declaration, pmacro::Context& context)
    -> Checked<std::string> {
    std::string result;
    const bool  debug   = enum_has_derive(declaration, "rstd::Debug");
    const bool  clone   = enum_has_derive(declaration, "rstd::Clone");
    const bool  error   = enum_has_derive(declaration, "rstd::Error");
    const bool  display = enum_has_derive(declaration, "rstd::Display");
    if (error && display) return enum_fail(declaration.span, "Error already derives Display");
    std::string units;
    std::string debug_names;
    for (const auto& variant : declaration.variants) {
        auto checked =
            enum_validate_attributes(declaration, variant.attributes, "variant", context);
        if (! checked) return std::unexpected(checked.error());
        for (const auto& field : variant.fields) {
            checked = enum_validate_attributes(declaration, field.attributes, "field", context);
            if (! checked) return std::unexpected(checked.error());
        }
        if (debug) {
            auto config =
                enum_owner_options(variant.attributes, Trait::Debug, Target::Record, context);
            if (! config) return std::unexpected(config.error());
            if (config->tuple &&
                (variant.shape == EnumShape::Unit || variant.shape == EnumShape::External))
                return enum_fail(variant.span,
                                 "tuple Debug requires an owned tuple or record payload");
            if (config->rename)
                debug_names += ",::rstd::derive::DebugVariantName<" + quote(variant.name) + ',' +
                               quote(*config->rename) + '>';
        }
        if (variant.shape == EnumShape::External) {
            auto config = enum_owner_options(
                variant.attributes, error ? Trait::Error : Trait::Display, Target::Record, context);
            if (! config) return std::unexpected(config.error());
            if (config->format || config->transparent)
                return enum_fail(variant.span, "external payload formatting belongs to its owner");
            continue;
        }
        if (variant.shape == EnumShape::Unit) {
            if (error || display) {
                auto config = enum_owner_options(variant.attributes,
                                                 error ? Trait::Error : Trait::Display,
                                                 Target::Record,
                                                 context);
                if (! config) return std::unexpected(config.error());
                if (! config->format || config->transparent)
                    return enum_fail(variant.span,
                                     "unit variant '" + variant.name + "' requires a format");
                auto mapped = map_format(*config->format, {});
                if (! mapped) return enum_fail(variant.span, mapped.error());
                units += ",::rstd::derive::UnitFormat<" + quote(variant.name) + ',' +
                         quote(mapped->text) + '>';
            }
            continue;
        }
        const auto payload = enum_payload_type(declaration, variant);
        if (debug) {
            auto config =
                enum_owner_options(variant.attributes, Trait::Debug, Target::Record, context);
            if (! config) return std::unexpected(config.error());
            auto                     fields = enum_fields_definition(declaration, variant);
            std::string              definition;
            bool                     customized = config->tuple || config->rename.has_value();
            std::vector<std::string> selected;
            for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                auto options = enum_owner_options(
                    variant.fields[i].attributes, Trait::Debug, Target::Field, context);
                if (! options) return std::unexpected(options.error());
                customized = customized || options->skip || options->rename.has_value();
                if (options->skip) continue;
                if (options->rename)
                    selected.push_back(
                        "::rstd::derive::Field<" + quote(*options->rename) + ",[](const " +
                        payload + "& rstd_enum_value)->decltype(auto){using rstd_enum_field=" +
                        fields[i].definition + ";return rstd_enum_field::get(rstd_enum_value);}>");
                else
                    selected.push_back(fields[i].definition);
            }
            if (! customized)
                definition = "::rstd::derive::DebugPayload<" + payload + '>';
            else {
                definition = "::rstd::derive::" +
                             std::string(variant.shape == EnumShape::Tuple || config->tuple
                                             ? "DebugTuple<"
                                             : "DebugRecord<") +
                             payload + ',' + quote(config->rename.value_or(variant.name));
                for (const auto& field : selected) definition += ',' + field;
                definition += '>';
            }
            result += enum_adl(declaration, payload, "rstd_debug_definition", definition);
        }
        if (clone)
            result += enum_adl(declaration,
                               payload,
                               "rstd_clone_definition",
                               "::rstd::derive::ClonePayload<" + payload + '>');
        if (error || display) {
            auto config = enum_owner_options(
                variant.attributes, error ? Trait::Error : Trait::Display, Target::Record, context);
            if (! config) return std::unexpected(config.error());
            auto fields = enum_fields_definition(declaration, variant);
            if (error) {
                std::optional<std::size_t> source;
                bool                       from = false;
                for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                    auto options = enum_owner_options(
                        variant.fields[i].attributes, Trait::Error, Target::Field, context);
                    if (! options) return std::unexpected(options.error());
                    if (options->source || options->from || variant.fields[i].name == "source") {
                        if (source)
                            return enum_fail(variant.fields[i].span,
                                             "multiple source fields are not supported");
                        if (config->transparent && options->source)
                            return enum_fail(variant.fields[i].span,
                                             "transparent cannot contain an explicit source field");
                        source = i;
                        from   = options->from;
                    }
                }
                auto description = error_record_description(
                    payload, *config, fields, source, from, variant.shape == EnumShape::Tuple);
                if (! description) return enum_fail(variant.span, description.error());
                result += enum_adl(
                    declaration, payload, "rstd_error_definition", description->definition);
                result += enum_adl(declaration,
                                   payload,
                                   "rstd_display_definition",
                                   description->display_definition);
            } else {
                if (! config->format)
                    return enum_fail(variant.span, "payload requires display format");
                auto format =
                    map_format(*config->format, fields, variant.shape == EnumShape::Tuple);
                if (! format) return enum_fail(variant.span, format.error());
                std::string definition =
                    "::rstd::derive::DisplayRecord<" + payload + ',' + quote(format->text);
                for (auto index : format->fields) definition += ',' + fields[index].definition;
                result +=
                    enum_adl(declaration, payload, "rstd_display_definition", definition + '>');
            }
        }
    }
    const auto type = enum_type(declaration);
    if (debug)
        result += enum_adl(declaration,
                           type,
                           "rstd_debug_definition",
                           "::rstd::derive::DebugVariants<" + type + debug_names + '>');
    if (clone)
        result += enum_adl(declaration,
                           type,
                           "rstd_clone_definition",
                           "::rstd::derive::CloneVariants<" + type + '>');
    if (error || display) {
        const auto definition =
            "::rstd::derive::" + std::string(error ? "ErrorVariants<" : "DisplayVariants<") + type +
            units + '>';
        result += enum_adl(declaration, type, "rstd_display_definition", definition);
        if (error) result += enum_adl(declaration, type, "rstd_error_definition", definition);
    }
    return result;
}
auto enum_declaration(pmacro::FunctionInput input, pmacro::Context& context) -> Output {
    auto declaration = parse_enum(input, context);
    if (! declaration) return failure(context, input.invocation_span, declaration.error());
    for (const auto& trait : declaration->derives)
        if (trait.starts_with("rstd::") && trait != "rstd::Debug" && trait != "rstd::Clone" &&
            trait != "rstd::Error" && trait != "rstd::Display")
            return failure(context,
                           input.invocation_span,
                           Error { enum_source(declaration->span),
                                   "rstd::Enum: unsupported derive '" + trait + "'" });
    auto attrs = enum_validate_attributes(*declaration, declaration->attributes, "type", context);
    if (! attrs) return failure(context, input.invocation_span, attrs.error());
    if (! declaration->parameters.empty() && ! enum_foreign(*declaration).empty())
        return failure(
            context,
            input.invocation_span,
            Error { enum_source(declaration->span),
                    "rstd::Enum: external derives on template enums are not supported yet" });
    auto helpers = enum_helpers(declaration->attributes, context);
    if (! helpers) return failure(context, input.invocation_span, helpers.error());
    EnumOutput  output;
    std::string source = enum_template(*declaration) + "class " + declaration->name + ';';
    for (const auto& variant : declaration->variants) {
        auto generated = enum_emit_payload(*declaration, variant, *helpers, output, context);
        if (! generated) return failure(context, input.invocation_span, generated.error());
        source += *generated;
    }
    auto body = enum_emit_class(*declaration, *helpers, output, context);
    if (! body) return failure(context, input.invocation_span, body.error());
    source += *body;
    auto derives = enum_emit_derives(*declaration, context);
    if (! derives) return failure(context, input.invocation_span, derives.error());
    source += *derives;
    if (input.after_item_export == pmacro::AfterItemExport::Required)
        source = "export {" + source + '}';
    auto scaffold = context.parse(source, declaration->span);
    if (! scaffold) return std::unexpected(scaffold.error());
    return output.compose(*scaffold, context);
}
auto enum_check(pmacro::DeriveInput input, pmacro::Context& context) -> Output {
    auto snapshot = context.declaration();
    if (! snapshot)
        return failure(
            context, input.item_span, Error { {}, "rstd::Enum: declaration is unavailable" });
    const auto* record = std::get_if<pmacro::RecordInfo>(&(*snapshot)->data);
    if (! record || record->has_bases)
        return failure(context,
                       input.item_span,
                       Error { (*snapshot)->source, "rstd::Enum: inheritance is not supported" });
    for (const auto& field : record->fields)
        if (! field.name || *field.name != "rstd_enum_choice_")
            return failure(
                context,
                input.item_span,
                Error { field.source, "rstd::Enum: extra instance fields are not supported" });
    auto output = context.stream();
    if (! output) return std::unexpected(pmacro::Error::Failure);
    return *output;
}
} // namespace rstd::macro
