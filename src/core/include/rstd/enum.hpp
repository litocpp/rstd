#pragma once

#define RSTD_ENUM_DETAIL_UNPAREN(...) __VA_ARGS__

#define RSTD_ENUM_DETAIL_PAYLOAD(Name, Fields) \
    struct Name##_payload {                    \
        RSTD_ENUM_DETAIL_UNPAREN Fields        \
    };

#define RSTD_ENUM_DETAIL_TAG(Name, Fields) Name,

#define RSTD_ENUM_DETAIL_COUNT(Name, Fields) +1

#define RSTD_ENUM_DETAIL_STORAGE_TYPE(Name, Fields) Name##_payload,

#define RSTD_TAG_ENUM_DETAIL_TAG(Name) Name,

#define RSTD_TAG_ENUM_DETAIL_COUNT(Name) +1

#define RSTD_ENUM_IN_PLACE(Name) \
    ::rstd::enum_detail::in_place_index<static_cast<::rstd::usize>(Tag::Name)>

#define RSTD_ENUM_INIT(Name, ...) \
    rstd_enum_storage_(RSTD_ENUM_IN_PLACE(Name) __VA_OPT__(, ) __VA_ARGS__)

#define RSTD_ENUM_DETAIL_FACTORY(Name, Fields)                                                  \
    template<typename... rstd_enum_detail_Args>                                                 \
        requires ::rstd::mtp::init<Name##_payload, rstd_enum_detail_Args...>                    \
    [[nodiscard]]                                                                               \
    static constexpr Self Name(rstd_enum_detail_Args&&... args) noexcept(                       \
        ::rstd::mtp::noex_init<Name##_payload, rstd_enum_detail_Args...>) {                     \
        return Self(RSTD_ENUM_IN_PLACE(Name), ::rstd::forward<rstd_enum_detail_Args>(args)...); \
    }

#define RSTD_ENUM_DETAIL_REPLACE(Name, Fields)                                                  \
    template<typename... rstd_enum_detail_Args>                                                 \
    constexpr void replace_##Name(rstd_enum_detail_Args&&... args) {                            \
        rstd_enum_storage_.replace(RSTD_ENUM_IN_PLACE(Name),                                    \
                                   ::rstd::forward<rstd_enum_detail_Args>(args)...);            \
    }

#define RSTD_ENUM_DETAIL_ACCESSOR(Name, Fields)                                          \
    [[nodiscard]]                                                                        \
    constexpr bool is_##Name() const noexcept {                                          \
        return rstd_enum_storage_.is(RSTD_ENUM_IN_PLACE(Name));                          \
    }                                                                                    \
                                                                                         \
    [[nodiscard]]                                                                        \
    constexpr Name##_payload& as_##Name() & noexcept {                                   \
        if (! is_##Name()) {                                                             \
            ::rstd::enum_detail::bad_enum_state();                                       \
        }                                                                                \
        return rstd_enum_storage_.get(RSTD_ENUM_IN_PLACE(Name));                         \
    }                                                                                    \
                                                                                         \
    [[nodiscard]]                                                                        \
    constexpr Name##_payload const& as_##Name() const& noexcept {                        \
        if (! is_##Name()) {                                                             \
            ::rstd::enum_detail::bad_enum_state();                                       \
        }                                                                                \
        return rstd_enum_storage_.get(RSTD_ENUM_IN_PLACE(Name));                         \
    }                                                                                    \
                                                                                         \
    [[nodiscard]]                                                                        \
    constexpr Name##_payload&& as_##Name() && noexcept {                                 \
        if (! is_##Name()) {                                                             \
            ::rstd::enum_detail::bad_enum_state();                                       \
        }                                                                                \
        return ::rstd::move(rstd_enum_storage_).get(RSTD_ENUM_IN_PLACE(Name));           \
    }                                                                                    \
                                                                                         \
    [[nodiscard]]                                                                        \
    constexpr Name##_payload const&& as_##Name() const&& noexcept {                      \
        if (! is_##Name()) {                                                             \
            ::rstd::enum_detail::bad_enum_state();                                       \
        }                                                                                \
        return ::rstd::move(rstd_enum_storage_).get(RSTD_ENUM_IN_PLACE(Name));           \
    }

#define RSTD_TAG_ENUM_DETAIL_FACTORY(Name)     \
    [[nodiscard]]                              \
    static constexpr Self Name() noexcept {    \
        return Self(RSTD_ENUM_IN_PLACE(Name)); \
    }

#define RSTD_TAG_ENUM_DETAIL_REPLACE(Name)                    \
    constexpr void replace_##Name() noexcept {                \
        rstd_enum_storage_.replace(RSTD_ENUM_IN_PLACE(Name)); \
    }

#define RSTD_TAG_ENUM_DETAIL_ACCESSOR(Name)                         \
    [[nodiscard]]                                                   \
    constexpr bool is_##Name() const noexcept {                     \
        return rstd_enum_storage_.is(RSTD_ENUM_IN_PLACE(Name));     \
    }

#define RSTD_ENUM_SELF(ClassName) using Self = ClassName;

#define RSTD_ENUM_VARIANT_COUNT(VARIANTS) \
    static constexpr ::rstd::usize rstd_enum_variant_count = 0 VARIANTS(RSTD_ENUM_DETAIL_COUNT);
#define RSTD_ENUM_INDEX_TYPE() \
    using rstd_enum_index_type = ::rstd::enum_detail::smallest_index_t<rstd_enum_variant_count>;

#define RSTD_ENUM_PAYLOAD_TYPES(VARIANTS) VARIANTS(RSTD_ENUM_DETAIL_PAYLOAD)

#define RSTD_ENUM_TAG_TYPE(VARIANTS)                                                              \
    enum class Tag : rstd_enum_index_type                                                         \
    {                                                                                             \
        VARIANTS(RSTD_ENUM_DETAIL_TAG)                                                            \
    };

#define RSTD_ENUM_VARIANT_TYPES(VARIANTS) \
    RSTD_ENUM_VARIANT_COUNT(VARIANTS)     \
                                           \
    RSTD_ENUM_INDEX_TYPE()                \
                                           \
    RSTD_ENUM_PAYLOAD_TYPES(VARIANTS)     \
                                           \
    RSTD_ENUM_TAG_TYPE(VARIANTS)

#define RSTD_ENUM_VARIANTS(ClassName, VARIANTS) \
    RSTD_ENUM_SELF(ClassName)                   \
                                                 \
    RSTD_ENUM_VARIANT_TYPES(VARIANTS)

#define RSTD_ENUM_DEFAULT_STORAGE(VARIANTS)                                                       \
    using rstd_enum_storage_type = ::rstd::enum_detail::storage<VARIANTS(                         \
        RSTD_ENUM_DETAIL_STORAGE_TYPE)::rstd::enum_detail::sentinel>;

#define RSTD_TAG_ENUM_VARIANT_COUNT(VARIANTS)                                                     \
    static constexpr ::rstd::usize rstd_enum_variant_count = 0 VARIANTS(                          \
        RSTD_TAG_ENUM_DETAIL_COUNT);
#define RSTD_TAG_ENUM_TAG_TYPE(VARIANTS)                                                          \
    enum class Tag : rstd_enum_index_type                                                         \
    {                                                                                             \
        VARIANTS(RSTD_TAG_ENUM_DETAIL_TAG)                                                        \
    };

#define RSTD_TAG_ENUM_VARIANT_TYPES(VARIANTS) \
    RSTD_TAG_ENUM_VARIANT_COUNT(VARIANTS)     \
                                               \
    RSTD_ENUM_INDEX_TYPE()                    \
                                               \
    RSTD_TAG_ENUM_TAG_TYPE(VARIANTS)

#define RSTD_TAG_ENUM_DEFAULT_STORAGE() \
    using rstd_enum_storage_type = ::rstd::enum_detail::tag_storage<rstd_enum_variant_count>;

#define RSTD_ENUM_TYPES(ClassName, VARIANTS) \
    RSTD_ENUM_VARIANTS(ClassName, VARIANTS)  \
                                              \
    RSTD_ENUM_DEFAULT_STORAGE(VARIANTS)

#define RSTD_TAG_ENUM_TYPES(ClassName, VARIANTS) \
    RSTD_ENUM_SELF(ClassName)                    \
                                                  \
    RSTD_TAG_ENUM_VARIANT_TYPES(VARIANTS)        \
                                                  \
    RSTD_TAG_ENUM_DEFAULT_STORAGE()

#define RSTD_ENUM_STORAGE(ClassName)                                                        \
    rstd_enum_storage_type rstd_enum_storage_;                                               \
                                                                                             \
    template<::rstd::usize I, typename... rstd_enum_detail_Args>                             \
    explicit constexpr ClassName(                                                            \
        ::rstd::enum_detail::in_place_index_t<I> in_place,                                   \
        rstd_enum_detail_Args&&... args)                                                      \
        noexcept(::rstd::mtp::noex_init<rstd_enum_storage_type,                               \
                                        ::rstd::enum_detail::in_place_index_t<I>,             \
                                        rstd_enum_detail_Args...>)                            \
        : rstd_enum_storage_(in_place, ::rstd::forward<rstd_enum_detail_Args>(args)...) {}

#define RSTD_ENUM_FACTORIES(VARIANTS) VARIANTS(RSTD_ENUM_DETAIL_FACTORY)

#define RSTD_ENUM_REPLACERS(VARIANTS) VARIANTS(RSTD_ENUM_DETAIL_REPLACE)

#define RSTD_TAG_ENUM_FACTORIES(VARIANTS) VARIANTS(RSTD_TAG_ENUM_DETAIL_FACTORY)

#define RSTD_TAG_ENUM_REPLACERS(VARIANTS) VARIANTS(RSTD_TAG_ENUM_DETAIL_REPLACE)

#define RSTD_ENUM_OBSERVERS()                                                 \
    [[nodiscard]]                                                             \
    constexpr ::rstd::usize index() const noexcept {                           \
        auto const i = rstd_enum_storage_.index();                             \
        if (i >= rstd_enum_variant_count) {                                    \
            ::rstd::enum_detail::bad_enum_state();                             \
        }                                                                      \
        return i;                                                              \
    }                                                                          \
                                                                               \
    [[nodiscard]]                                                              \
    constexpr Tag tag() const noexcept {                                       \
        return static_cast<Tag>(index());                                      \
    }

#define RSTD_ENUM_ACCESSORS(VARIANTS) VARIANTS(RSTD_ENUM_DETAIL_ACCESSOR)

#define RSTD_TAG_ENUM_ACCESSORS(VARIANTS) VARIANTS(RSTD_TAG_ENUM_DETAIL_ACCESSOR)

#define RSTD_ENUM_INLINE(ClassName, VARIANTS) \
public:                                      \
    RSTD_ENUM_TYPES(ClassName, VARIANTS)     \
                                             \
private:                                     \
    RSTD_ENUM_STORAGE(ClassName)             \
                                             \
public:                                      \
    RSTD_ENUM_FACTORIES(VARIANTS)            \
                                             \
    RSTD_ENUM_REPLACERS(VARIANTS)            \
                                             \
    RSTD_ENUM_OBSERVERS()                    \
                                             \
    RSTD_ENUM_ACCESSORS(VARIANTS)

#define RSTD_TAG_ENUM_INLINE(ClassName, VARIANTS) \
public:                                          \
    RSTD_TAG_ENUM_TYPES(ClassName, VARIANTS)     \
                                                 \
private:                                         \
    RSTD_ENUM_STORAGE(ClassName)                 \
                                                 \
public:                                          \
    RSTD_TAG_ENUM_FACTORIES(VARIANTS)            \
                                                 \
    RSTD_TAG_ENUM_REPLACERS(VARIANTS)            \
                                                 \
    RSTD_ENUM_OBSERVERS()                        \
                                                 \
    RSTD_TAG_ENUM_ACCESSORS(VARIANTS)

#define RSTD_ENUM_BODY(ClassName, VARIANTS) RSTD_ENUM_INLINE(ClassName, VARIANTS)

#define RSTD_TAG_ENUM_BODY(ClassName, VARIANTS) RSTD_TAG_ENUM_INLINE(ClassName, VARIANTS)

#define RSTD_ENUM(ClassName, VARIANTS)      \
    class ClassName final {                 \
        RSTD_ENUM_BODY(ClassName, VARIANTS) \
    };

#define RSTD_TAG_ENUM(ClassName, VARIANTS)      \
    class ClassName final {                     \
        RSTD_TAG_ENUM_BODY(ClassName, VARIANTS) \
    };

#define RSTD_ENUM_TEMPLATE(TemplateParams, ClassName, VARIANTS) \
    template<RSTD_ENUM_DETAIL_UNPAREN TemplateParams>           \
    class ClassName final {                                     \
        RSTD_ENUM_BODY(ClassName, VARIANTS)                     \
    };

#define RSTD_TAG_ENUM_TEMPLATE(TemplateParams, ClassName, VARIANTS) \
    template<RSTD_ENUM_DETAIL_UNPAREN TemplateParams>               \
    class ClassName final {                                         \
        RSTD_TAG_ENUM_BODY(ClassName, VARIANTS)                     \
    };

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
