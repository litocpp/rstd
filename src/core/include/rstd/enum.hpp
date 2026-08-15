#pragma once

#include <rstd/macro.hpp>

#define RSTD_CHOICE_DETAIL_CASE_ENTRY(entry)   RSTD_DETAIL_APPLY(RSTD_CHOICE_DETAIL_CASE, entry)
#define RSTD_CHOICE_DETAIL_CASE(TagValue, ...) ::rstd::choice_case<TagValue, __VA_ARGS__>

#define RSTD_CHOICE_TYPES(...) \
    RSTD_DETAIL_FOR_EACH(      \
        RSTD_CHOICE_DETAIL_CASE_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_COMMA, __VA_ARGS__)

#define RSTD_ENUM_DETAIL_COUNT_ENTRY(entry) +1

#define RSTD_ENUM_DETAIL_PAYLOAD_ENTRY(entry) RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_PAYLOAD, entry)
#define RSTD_ENUM_DETAIL_PAYLOAD(Name, ...) \
    __VA_OPT__(struct Name##_payload { RSTD_DETAIL_UNPAREN __VA_ARGS__ };)

#define RSTD_ENUM_DETAIL_TAG_ENTRY(entry) RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_TAG, entry)
#define RSTD_ENUM_DETAIL_TAG(Name, ...)   Name,

#define RSTD_ENUM_DETAIL_CASE_ENTRY(entry) RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_CASE, entry)
#define RSTD_ENUM_DETAIL_CASE(Name, ...) \
    ::rstd::choice_case<Tag::Name, RSTD_DETAIL_IF_ARGS(__VA_ARGS__)(Name##_payload, void)>

#define RSTD_ENUM_DETAIL_FACTORY_ENTRY(entry) \
    RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_FACTORY_SELECT, entry)
#define RSTD_ENUM_DETAIL_FACTORY_SELECT(Name, ...)                     \
    RSTD_DETAIL_IF_ARGS(__VA_ARGS__)(RSTD_ENUM_DETAIL_FACTORY_PAYLOAD, \
                                     RSTD_ENUM_DETAIL_FACTORY_EMPTY)(Name)
#define RSTD_ENUM_DETAIL_FACTORY_EMPTY(Name)             \
    [[nodiscard]]                                        \
    static constexpr auto Name() noexcept -> Self {      \
        return Self(rstd_enum_in_place_t<Tag::Name> {}); \
    }
#define RSTD_ENUM_DETAIL_FACTORY_PAYLOAD(Name)                                        \
    template<typename... rstd_enum_detail_Args>                                       \
        requires ::rstd::mtp::init<Name##_payload, rstd_enum_detail_Args...>          \
    [[nodiscard]]                                                                     \
    static constexpr auto Name(rstd_enum_detail_Args&&... args) noexcept(             \
        ::rstd::mtp::noex_init_v<Name##_payload, rstd_enum_detail_Args...>) -> Self { \
        return Self(rstd_enum_in_place_t<Tag::Name> {},                               \
                    ::rstd::forward<rstd_enum_detail_Args>(args)...);                 \
    }

#define RSTD_ENUM_DETAIL_REPLACE_ENTRY(entry) \
    RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_REPLACE_SELECT, entry)
#define RSTD_ENUM_DETAIL_REPLACE_SELECT(Name, ...)                     \
    RSTD_DETAIL_IF_ARGS(__VA_ARGS__)(RSTD_ENUM_DETAIL_REPLACE_PAYLOAD, \
                                     RSTD_ENUM_DETAIL_REPLACE_EMPTY)(Name)
#define RSTD_ENUM_DETAIL_REPLACE_EMPTY(Name)         \
    constexpr void replace_##Name() noexcept {       \
        rstd_enum_choice_.template set<Tag::Name>(); \
    }
#define RSTD_ENUM_DETAIL_REPLACE_PAYLOAD(Name)                                               \
    template<typename... rstd_enum_detail_Args>                                              \
        requires requires(rstd_enum_choice_type& choice, rstd_enum_detail_Args&&... args) {  \
            choice.template set<Tag::Name>(::rstd::forward<rstd_enum_detail_Args>(args)...); \
        }                                                                                    \
    constexpr void replace_##Name(rstd_enum_detail_Args&&... args) noexcept(                 \
        noexcept(rstd_enum_choice_.template set<Tag::Name>(                                  \
            ::rstd::forward<rstd_enum_detail_Args>(args)...))) {                             \
        rstd_enum_choice_.template set<Tag::Name>(                                           \
            ::rstd::forward<rstd_enum_detail_Args>(args)...);                                \
    }

#define RSTD_ENUM_DETAIL_PREDICATE_ENTRY(entry) RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_PREDICATE, entry)
#define RSTD_ENUM_DETAIL_PREDICATE(Name, ...)              \
    [[nodiscard]]                                          \
    constexpr auto is_##Name() const noexcept -> bool {    \
        return rstd_enum_choice_.template is<Tag::Name>(); \
    }

#define RSTD_ENUM_DETAIL_ACCESSOR_ENTRY(entry) \
    RSTD_DETAIL_APPLY(RSTD_ENUM_DETAIL_ACCESSOR_SELECT, entry)
#define RSTD_ENUM_DETAIL_ACCESSOR_SELECT(Name, ...) __VA_OPT__(RSTD_ENUM_DETAIL_ACCESSOR(Name))
#define RSTD_ENUM_DETAIL_ACCESSOR(Name)                                      \
    [[nodiscard]]                                                            \
    constexpr auto as_##Name() & noexcept -> Name##_payload& {               \
        return rstd_enum_choice_.template as<Tag::Name>();                   \
    }                                                                        \
                                                                             \
    [[nodiscard]]                                                            \
    constexpr auto as_##Name() const& noexcept -> Name##_payload const& {    \
        return rstd_enum_choice_.template as<Tag::Name>();                   \
    }                                                                        \
                                                                             \
    [[nodiscard]]                                                            \
    constexpr auto as_##Name() && noexcept -> Name##_payload&& {             \
        return ::rstd::move(rstd_enum_choice_).template as<Tag::Name>();     \
    }                                                                        \
                                                                             \
    [[nodiscard]]                                                            \
    constexpr auto as_##Name() const&& noexcept -> Name##_payload const&& {  \
        return static_cast<rstd_enum_choice_type const&&>(rstd_enum_choice_) \
            .template as<Tag::Name>();                                       \
    }

#define RSTD_ENUM_DETAIL_VISIT                                                                   \
    template<typename rstd_enum_detail_Visitor>                                                  \
        requires requires(rstd_enum_choice_type& choice, rstd_enum_detail_Visitor&& visitor) {   \
            choice.visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));                    \
        }                                                                                        \
    constexpr decltype(auto) visit(rstd_enum_detail_Visitor&& visitor) & noexcept(               \
        noexcept(rstd_enum_choice_.visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor)))) { \
        return rstd_enum_choice_.visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));      \
    }                                                                                            \
                                                                                                 \
    template<typename rstd_enum_detail_Visitor>                                                  \
        requires requires(const rstd_enum_choice_type& choice,                                   \
                          rstd_enum_detail_Visitor&&   visitor) {                                \
            choice.visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));                    \
        }                                                                                        \
    constexpr decltype(auto) visit(rstd_enum_detail_Visitor&& visitor) const& noexcept(          \
        noexcept(rstd_enum_choice_.visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor)))) { \
        return rstd_enum_choice_.visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));      \
    }                                                                                            \
                                                                                                 \
    template<typename rstd_enum_detail_Visitor>                                                  \
        requires requires(rstd_enum_choice_type&& choice, rstd_enum_detail_Visitor&& visitor) {  \
            ::rstd::move(choice).visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));      \
        }                                                                                        \
    constexpr decltype(auto) visit(rstd_enum_detail_Visitor&& visitor) && noexcept(              \
        noexcept(::rstd::move(rstd_enum_choice_)                                                 \
                     .visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor)))) {              \
        return ::rstd::move(rstd_enum_choice_)                                                   \
            .visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));                          \
    }                                                                                            \
                                                                                                 \
    template<typename rstd_enum_detail_Visitor>                                                  \
        requires requires(const rstd_enum_choice_type&& choice,                                  \
                          rstd_enum_detail_Visitor&&    visitor) {                               \
            static_cast<const rstd_enum_choice_type&&>(choice).visit(                            \
                ::rstd::forward<rstd_enum_detail_Visitor>(visitor));                             \
        }                                                                                        \
    constexpr decltype(auto) visit(rstd_enum_detail_Visitor&& visitor) const&& noexcept(         \
        noexcept(static_cast<const rstd_enum_choice_type&&>(rstd_enum_choice_)                   \
                     .visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor)))) {              \
        return static_cast<const rstd_enum_choice_type&&>(rstd_enum_choice_)                     \
            .visit(::rstd::forward<rstd_enum_detail_Visitor>(visitor));                          \
    }                                                                                            \
                                                                                                 \
    template<typename rstd_enum_detail_Visitor>                                                  \
        requires requires(rstd_enum_choice_type& choice, rstd_enum_detail_Visitor&& visitor) {   \
            choice.visit_mut(::rstd::forward<rstd_enum_detail_Visitor>(visitor));                \
        }                                                                                        \
    constexpr decltype(auto) visit_mut(rstd_enum_detail_Visitor&& visitor) & noexcept(noexcept(  \
        rstd_enum_choice_.visit_mut(::rstd::forward<rstd_enum_detail_Visitor>(visitor)))) {      \
        return rstd_enum_choice_.visit_mut(::rstd::forward<rstd_enum_detail_Visitor>(visitor));  \
    }                                                                                            \
                                                                                                 \
    template<typename rstd_enum_detail_Visitor>                                                  \
        requires requires(rstd_enum_choice_type&& choice, rstd_enum_detail_Visitor&& visitor) {  \
            ::rstd::move(choice).visit_mut(::rstd::forward<rstd_enum_detail_Visitor>(visitor));  \
        }                                                                                        \
    constexpr decltype(auto) visit_mut(rstd_enum_detail_Visitor&& visitor) && noexcept(          \
        noexcept(::rstd::move(rstd_enum_choice_)                                                 \
                     .visit_mut(::rstd::forward<rstd_enum_detail_Visitor>(visitor)))) {          \
        return ::rstd::move(rstd_enum_choice_)                                                   \
            .visit_mut(::rstd::forward<rstd_enum_detail_Visitor>(visitor));                      \
    }

#define RSTD_ENUM_DETAIL_MEMBERS(ClassName, ...)                                               \
public:                                                                                        \
    using Self                                              = ClassName;                       \
    static constexpr ::rstd::size_t rstd_enum_variant_count = 0 RSTD_DETAIL_FOR_EACH(          \
        RSTD_ENUM_DETAIL_COUNT_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__);       \
    RSTD_DETAIL_FOR_EACH(                                                                      \
        RSTD_ENUM_DETAIL_PAYLOAD_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__)      \
    enum class Tag : ::rstd::mtp::cond<(rstd_enum_variant_count <= 0xff),                      \
                                       ::rstd::uint8_t,                                        \
                                       ::rstd::mtp::cond<(rstd_enum_variant_count <= 0xffff),  \
                                                         ::rstd::uint16_t,                     \
                                                         ::rstd::uint32_t>>                    \
    {                                                                                          \
        RSTD_DETAIL_FOR_EACH(                                                                  \
            RSTD_ENUM_DETAIL_TAG_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__)      \
    };                                                                                         \
                                                                                               \
private:                                                                                       \
    using rstd_enum_choice_type = ::rstd::Choice<RSTD_DETAIL_FOR_EACH(                         \
        RSTD_ENUM_DETAIL_CASE_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_COMMA, __VA_ARGS__)>;      \
    template<Tag Value>                                                                        \
    struct rstd_enum_in_place_t {};                                                            \
    rstd_enum_choice_type rstd_enum_choice_;                                                   \
                                                                                               \
    template<Tag Value, typename... rstd_enum_detail_Args>                                     \
    explicit constexpr ClassName(                                                              \
        rstd_enum_in_place_t<Value>,                                                           \
        rstd_enum_detail_Args&&... args) noexcept(noexcept(rstd_enum_choice_type::             \
                                                               template with<Value>(           \
                                                                   ::rstd::forward<            \
                                                                       rstd_enum_detail_Args>( \
                                                                       args)...)))             \
        : rstd_enum_choice_(rstd_enum_choice_type::template with<Value>(                       \
              ::rstd::forward<rstd_enum_detail_Args>(args)...)) {                              \
    }                                                                                          \
                                                                                               \
public:                                                                                        \
    RSTD_DETAIL_FOR_EACH(                                                                      \
        RSTD_ENUM_DETAIL_FACTORY_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__)      \
    RSTD_DETAIL_FOR_EACH(                                                                      \
        RSTD_ENUM_DETAIL_REPLACE_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__)      \
    RSTD_DETAIL_FOR_EACH(                                                                      \
        RSTD_ENUM_DETAIL_PREDICATE_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__)    \
    RSTD_DETAIL_FOR_EACH(                                                                      \
        RSTD_ENUM_DETAIL_ACCESSOR_ENTRY, RSTD_DETAIL_FOR_EACH_SEPARATOR_NONE, __VA_ARGS__)     \
    RSTD_ENUM_DETAIL_VISIT                                                                     \
    [[nodiscard]]                                                                              \
    constexpr auto index() const noexcept -> ::rstd::size_t {                                  \
        return rstd_enum_choice_.index();                                                      \
    }                                                                                          \
    [[nodiscard]]                                                                              \
    constexpr auto tag() const noexcept -> Tag {                                               \
        return rstd_enum_choice_.which();                                                      \
    }

#define RSTD_ENUM(ClassName, ...) RSTD_ENUM_DETAIL_MEMBERS(ClassName, __VA_ARGS__)

#define RSTD_ENUM_DETAIL_DEFAULT_CTOR(ClassName, Name, ...)                                 \
    constexpr ClassName() noexcept(                                                         \
        noexcept(rstd_enum_choice_type::template with<Tag::Name>(__VA_ARGS__)))             \
        : rstd_enum_choice_(rstd_enum_choice_type::template with<Tag::Name>(__VA_ARGS__)) { \
    }

#define RSTD_ENUM_DEFAULT(ClassName, DefaultEntry, ...) \
    RSTD_ENUM_DETAIL_MEMBERS(ClassName, __VA_ARGS__)    \
    RSTD_ENUM_DETAIL_DEFAULT_CTOR_APPLY(ClassName, DefaultEntry)

#define RSTD_ENUM_DETAIL_DEFAULT_CTOR_APPLY(ClassName, DefaultEntry) \
    RSTD_ENUM_DETAIL_DEFAULT_CTOR_APPLY_I(ClassName, RSTD_DETAIL_UNPAREN DefaultEntry)
#define RSTD_ENUM_DETAIL_DEFAULT_CTOR_APPLY_I(ClassName, ...) \
    RSTD_ENUM_DETAIL_DEFAULT_CTOR(ClassName, __VA_ARGS__)

#define RSTD_MATCH(value) switch (auto&& matched = (value); matched.tag())
#define RSTD_CASE(Name, ...)                                                             \
    break;                                                                               \
    case ::rstd::mtp::rm_cvf<decltype(matched)>::Tag::Name:                              \
        if (__VA_OPT__(auto&& [__VA_ARGS__] =                                            \
                           ::rstd::enum_detail::forward_like<decltype(matched)>(matched) \
                               .as_##Name();) true)

#define RSTD_CASE_PAYLOAD(Name, var)                                                       \
    break;                                                                                 \
    case ::rstd::mtp::rm_cvf<decltype(matched)>::Tag::Name:                                \
        if (auto&& var =                                                                   \
                ::rstd::enum_detail::forward_like<decltype(matched)>(matched).as_##Name(); \
            true)
