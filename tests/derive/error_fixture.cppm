module;
#include <rstd/enum.hpp>

export module rstd.tests.derive.errors;

import rstd.core;
import rstd.alloc;

export namespace derive_fixture::errors
{
struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("cause {value}"))]] Cause {
    alloc::string::String value;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Debug", tuple),
         pmacro::helper("rstd::Error", format("{cause}\ncontext\r\t\\\""))]] Payload {
    [[pmacro::helper("rstd::Error", from)]]
    Cause cause;
};

class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants, unit("Unit", "not the variant name"))]] Failure {
    RSTD_ENUM_TYPES(Failure, (Unit), (Wrapped, (::derive_fixture::errors::Payload)))

public:
    explicit Failure(Cause cause);
};

inline Failure::Failure(Cause cause): Failure(Wrapped(Payload { rstd::move(cause) })) {
}

class Recursive;

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Debug", tuple),
         pmacro::helper("rstd::Error", format("{cause}"))]] RecursiveLeaf {
    [[pmacro::helper("rstd::Error", from)]]
    Cause cause;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Error", format("{error}\n{context}"))]] RecursiveContext {
    alloc::string::String context;
    [[pmacro::helper("rstd::Error", source)]]
    alloc::sync::Arc<Recursive> error;
};

class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants, unit("Unit", "unit error"))]] Recursive {
    RSTD_ENUM_TYPES(Recursive, (Unit), (Leaf, (RecursiveLeaf)), (WithContext, (RecursiveContext)))

public:
    explicit Recursive(Cause cause);
    auto root() const& -> const Recursive&;
    auto with_context(rstd::ref<rstd::str> context) && -> Recursive;
};

inline Recursive::Recursive(Cause cause): Recursive(Leaf(RecursiveLeaf { rstd::move(cause) })) {
}

inline auto Recursive::root() const& -> const Recursive& {
    auto* error = this;
    while (error->is_WithContext()) error = rstd::addressof(*error->as_WithContext().error);
    return *error;
}

inline auto Recursive::with_context(rstd::ref<rstd::str> context) && -> Recursive {
    return WithContext(alloc::string::String::make(context),
                       alloc::sync::Arc<Recursive>::make(rstd::move(*this)));
}

struct [[pmacro::derive("rstd::Debug", "rstd::Error"),
         pmacro::helper("rstd::Debug", tuple),
         pmacro::helper("rstd::Error", format("{cause}"))]] SharedPayload {
    [[pmacro::helper("rstd::Error", from)]]
    alloc::sync::Arc<Cause> cause;
};

class [[pmacro::derive("rstd::Debug", "rstd::Error"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Error", variants, unit("Unit", "shared unit"))]] SharedFailure {
    RSTD_ENUM_TYPES(SharedFailure, (Unit), (Shared, (SharedPayload)))

public:
    auto clone() const -> SharedFailure;
    auto has_cause() const -> bool;
};

inline auto SharedFailure::clone() const -> SharedFailure {
    RSTD_MATCH(*this) {
        RSTD_CASE(Unit) {
            return Unit();
        }
        RSTD_CASE(Shared, cause) {
            return Shared(cause.clone());
        }
    }
}

inline auto SharedFailure::has_cause() const -> bool {
    RSTD_MATCH(*this) {
        RSTD_CASE(Unit) {
            return false;
        }
        RSTD_CASE(Shared, cause) {
            return ! cause->value.is_empty();
        }
    }
}
} // namespace derive_fixture::errors
