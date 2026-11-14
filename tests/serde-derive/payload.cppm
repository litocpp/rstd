module;
#include <rstd/enum.hpp>
export module rstd.tests.serde_payload;
import rstd;
import rstd.serde;

using namespace rstd::prelude;

export namespace serde_payload
{
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", described_payload, name("Scalar"))]] Scalar {
    u64                   label;
    friend constexpr auto rstd_payload_definition(Scalar*) {
        return rstd::VariantPayload<Scalar,
                                    rstd::VariantKind::Tuple,
                                    "Scalar",
                                    rstd::PayloadField<"label", &Scalar::label>> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", described_payload, name("Pair"))]] Pair {
    u64                   first;
    String                second;
    friend constexpr auto rstd_payload_definition(Pair*) {
        return rstd::VariantPayload<Pair,
                                    rstd::VariantKind::Tuple,
                                    "Pair",
                                    rstd::PayloadField<"first", &Pair::first>,
                                    rstd::PayloadField<"second", &Pair::second>> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", described_payload, name("Tuple"))]] EmptyTuple {
    friend constexpr auto rstd_payload_definition(EmptyTuple*) {
        return rstd::VariantPayload<EmptyTuple, rstd::VariantKind::Tuple, "Tuple"> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", described_payload, name("Record"))]] EmptyRecord {
    friend constexpr auto rstd_payload_definition(EmptyRecord*) {
        return rstd::VariantPayload<EmptyRecord, rstd::VariantKind::Record, "Record"> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive", described_payload, name("Unit"))]] Unit {
    friend constexpr auto rstd_payload_definition(Unit*) {
        return rstd::VariantPayload<Unit, rstd::VariantKind::Unit, "Unit"> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive",
                        described_payload,
                        name("Fields"),
                        parent(ignore_unknown),
                        variant(reject_unknown))]] Fields {
    [[pmacro::helper("serde::derive", rename("wire"), alias("legacy"))]]
    u64 number;
    [[pmacro::helper("serde::derive", default)]]
    Option<String> note;
    [[pmacro::helper("serde::derive", skip)]]
    u64                   hidden;
    friend constexpr auto rstd_payload_definition(Fields*) {
        return rstd::VariantPayload<Fields,
                                    rstd::VariantKind::Record,
                                    "Fields",
                                    rstd::PayloadField<"note", &Fields::note>,
                                    rstd::PayloadField<"number", &Fields::number>,
                                    rstd::PayloadField<"hidden", &Fields::hidden>> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive",
                        described_payload,
                        name("Inherited"),
                        parent(ignore_unknown, tag("kind"), rename("Root")))]] Inherited {
    u64                   value;
    friend constexpr auto rstd_payload_definition(Inherited*) {
        return rstd::VariantPayload<Inherited,
                                    rstd::VariantKind::Record,
                                    "Inherited",
                                    rstd::PayloadField<"value", &Inherited::value>> {};
    }
};
struct [[pmacro::derive("serde::derive"),
         pmacro::helper("serde::derive",
                        described_payload,
                        name("Directed"),
                        parent(encode_only),
                        variant(decode_only))]] Directed {
    u64                   value;
    friend constexpr auto rstd_payload_definition(Directed*) {
        return rstd::VariantPayload<Directed,
                                    rstd::VariantKind::Tuple,
                                    "Directed",
                                    rstd::PayloadField<"value", &Directed::value>> {};
    }
};
template<typename T>
struct Generic {
    T                     label;
    friend constexpr auto rstd_payload_definition(Generic*) {
        return rstd::VariantPayload<Generic,
                                    rstd::VariantKind::Tuple,
                                    "Generic",
                                    rstd::PayloadField<"label", &Generic::label>> {};
    }
    friend constexpr auto rstd_debug_definition(Generic*) {
        return rstd::derive::DebugPayload<Generic> {};
    }
    friend constexpr auto rstd_clone_definition(Generic*) {
        return rstd::derive::ClonePayload<Generic> {};
    }
};
constexpr auto rstd_debug_definition(Unit*) {
    return rstd::derive::DebugPayload<Unit> {};
}
constexpr auto rstd_debug_definition(EmptyTuple*) {
    return rstd::derive::DebugPayload<EmptyTuple> {};
}
constexpr auto rstd_debug_definition(EmptyRecord*) {
    return rstd::derive::DebugPayload<EmptyRecord> {};
}
constexpr auto rstd_debug_definition(Pair*) {
    return rstd::derive::DebugPayload<Pair> {};
}
constexpr auto rstd_clone_definition(Pair*) {
    return rstd::derive::ClonePayload<Pair> {};
}

class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive",
                       described_enum,
                       variant("Number", rename("number")),
                       variant("Idle", name("idle")))]] External {
    RSTD_ENUM_TYPES(External,
                    (Idle),
                    (Number, (Scalar)),
                    (Pair, (Pair)),
                    (Tuple, (EmptyTuple)),
                    (Record, (EmptyRecord)),
                    (Fields, (Fields)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive",
                       described_enum,
                       tag("kind"),
                       variant("Fields", rename("fields")))]] Internal {
    RSTD_ENUM_TYPES(Internal, (Idle), (Fields, (Fields)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive", described_enum, tag("kind"), ignore_unknown)]]
FlexibleInternal {
    RSTD_ENUM_TYPES(FlexibleInternal, (Idle), (Fields, (Fields)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive",
                       described_enum,
                       tag("kind"),
                       content("data"),
                       variant("Number", rename("number")))]] Adjacent {
    RSTD_ENUM_TYPES(Adjacent, (Idle), (Number, (Scalar)), (Pair, (Pair)))
};
class [[pmacro::derive("serde::derive"),
        pmacro::helper("serde::derive",
                       described_enum,
                       ignore_unknown,
                       variant("Inherited", ignore_unknown))]] FlexibleExternal {
    RSTD_ENUM_TYPES(FlexibleExternal, (Inherited, (Inherited)))
};
struct ExternalDebugPayload {
    i32 number;
};
constexpr auto rstd_debug_definition(ExternalDebugPayload*) {
    return rstd::derive::DebugRecord<
        ExternalDebugPayload,
        "OriginalExternalName",
        rstd::PayloadField<"number", &ExternalDebugPayload::number>> {};
}
class DebugNames {
    RSTD_ENUM_TYPES(DebugNames, (Idle), (Owned, (Pair)), (External, (ExternalDebugPayload)))
};
constexpr auto rstd_debug_definition(DebugNames*) {
    return rstd::derive::DebugVariants<
        DebugNames,
        rstd::derive::DebugVariantName<"Idle", "idle-label">,
        rstd::derive::DebugVariantName<"Owned", "OwnedLabel">,
        rstd::derive::DebugVariantName<"External", "ExternalLabel">> {};
}
} // namespace serde_payload
