export module rstd.core:type_id;
import :num.types;

export namespace rstd::any
{

class TypeId {
    template<typename T>
    struct Token {
        static inline constexpr char value {};
    };

    const void* value_;

    explicit constexpr TypeId(const void* value) noexcept: value_(value) {}

public:
    template<typename T>
    static constexpr auto of() noexcept -> TypeId {
        using U = mtp::rm_cvf<T>;
        return TypeId { &Token<U>::value };
    }

    friend constexpr auto operator==(TypeId, TypeId) noexcept -> bool = default;
};

} // namespace rstd::any
