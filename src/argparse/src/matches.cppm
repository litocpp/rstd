module;
#include <rstd/enum.hpp>

export module rstd.argparse:matches;
export import :arg;
export import :command_key;
import :schema;

using namespace rstd::prelude;
using rstd::ffi::OsString;
using rstd::sync::Arc;

struct MatchedArg {
    Vec<Box<dyn<rstd::any::Any>>> typed_values;
    Vec<OsString>                 raw_values;
    Vec<usize>                    indices;
    Vec<usize>                    occurrence_ends;
    usize                         occurrences {};
    bool                          from_default { false };

    MatchedArg()
        : typed_values(Vec<Box<dyn<rstd::any::Any>>>::make()),
          raw_values(Vec<OsString>::make()),
          indices(Vec<usize>::make()),
          occurrence_ends(Vec<usize>::make()) {}
};

struct GlobalMatchedArg {
    Arc<CompiledCommand> schema;
    usize                slot;
    MatchedArg           matched;
};

struct GlobalMatches {
    Vec<GlobalMatchedArg> args;

    GlobalMatches(): args(Vec<GlobalMatchedArg>::make()) {}
    explicit GlobalMatches(Vec<GlobalMatchedArg> values): args(rstd::move(values)) {}
};

export namespace rstd::argparse
{

class ValueSource final {
    RSTD_ENUM_DEFAULT(ValueSource, (CommandLine), (DefaultValue), (CommandLine))
};

template<typename T>
class Values : public DefaultInClass<Values<T>, iter::Iterator> {
    slice<Box<dyn<any::Any>>> values_;
    usize                     next_ {};

    explicit Values(slice<Box<dyn<any::Any>>> values): values_(values) {}
    friend class Matches;

public:
    using Item = ref<T>;

    auto next() -> Option<Item> {
        if (next_ == values_.len()) return None();
        auto value = any::downcast_ref<T>(values_[next_].as_ref());
        ++next_;
        return value;
    }

    auto len() const noexcept -> usize { return values_.len() - next_; }
    auto size_hint() const -> iter::SizeHint { return { len(), Some(len()) }; }
};

class Matches {
    Arc<CompiledCommand>       schema_;
    Vec<MatchedArg>            args_;
    Option<Arc<GlobalMatches>> global_matches_;
    Option<String>             subcommand_name_;
    Option<Box<Matches>>       subcommand_matches_;

    Matches(Arc<CompiledCommand> schema,
            Vec<MatchedArg>      args,
            Option<String>       subcommand_name,
            Option<Box<Matches>> subcommand_matches)
        : schema_(rstd::move(schema)),
          args_(rstd::move(args)),
          global_matches_(None()),
          subcommand_name_(rstd::move(subcommand_name)),
          subcommand_matches_(rstd::move(subcommand_matches)) {}

    friend class Parser;

    auto global_match(u64 command, usize slot) const -> Option<ref<GlobalMatchedArg>> {
        if (global_matches_.is_none()) return None();
        bool visible = false;
        for (const auto& spec : schema_->args) {
            if (spec.global && spec.owner_command == command && spec.owner_slot == slot) {
                visible = true;
                break;
            }
        }
        if (! visible) return None();
        for (const auto& argument : (*global_matches_)->args) {
            if (argument.schema->command_token == command && argument.slot == slot) {
                return Some(ref<GlobalMatchedArg>::from_raw_parts(rstd::addressof(argument)));
            }
        }
        return None();
    }

    auto global_match(ref<str> id) const -> Option<ref<GlobalMatchedArg>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_none() || ! schema_->args[**slot].global) return None();
        const auto& spec = schema_->args[**slot];
        return global_match(spec.owner_command, spec.owner_slot);
    }

public:
    Matches(const Matches&)            = delete;
    Matches& operator=(const Matches&) = delete;
    Matches(Matches&&)                 = default;
    Matches& operator=(Matches&&)      = default;

    template<typename T>
    auto get_one(const ArgKey<T>& key) const -> Result<Option<ref<T>>, MatchAccessError> {
        ref<ArgSpec>    spec;
        ref<MatchedArg> matched;
        if (key.command_ == schema_->command_token && key.slot_ < schema_->args.len() &&
            ! schema_->args[key.slot_].global) {
            spec    = ref<ArgSpec>::from_raw_parts(rstd::addressof(schema_->args[key.slot_]));
            matched = ref<MatchedArg>::from_raw_parts(rstd::addressof(args_[key.slot_]));
        } else {
            auto global = global_match(key.command_, key.slot_);
            if (global.is_none()) return Err(MatchAccessError::ForeignKey());
            spec = ref<ArgSpec>::from_raw_parts(
                rstd::addressof((*global)->schema->args[(*global)->slot]));
            matched = ref<MatchedArg>::from_raw_parts(rstd::addressof((*global)->matched));
        }
        if (spec->type_id != any::TypeId::of<T>()) {
            return Err(MatchAccessError::WrongType());
        }
        if (spec->action.is_Append() ||
            (spec->num_args.maximum().is_none() || *spec->num_args.maximum() > usize(1))) {
            return Err(MatchAccessError::IncompatibleAccessor());
        }
        if (matched->typed_values.is_empty()) return Ok(None());
        auto value = any::downcast_ref<T>(matched->typed_values[usize()].as_ref());
        if (value.is_none()) return Err(MatchAccessError::WrongType());
        return Ok(rstd::move(value));
    }

    template<typename T>
    auto get_many(const ArgKey<T>& key) const -> Result<Option<Values<T>>, MatchAccessError> {
        ref<ArgSpec>    spec;
        ref<MatchedArg> matched;
        if (key.command_ == schema_->command_token && key.slot_ < schema_->args.len() &&
            ! schema_->args[key.slot_].global) {
            spec    = ref<ArgSpec>::from_raw_parts(rstd::addressof(schema_->args[key.slot_]));
            matched = ref<MatchedArg>::from_raw_parts(rstd::addressof(args_[key.slot_]));
        } else {
            auto global = global_match(key.command_, key.slot_);
            if (global.is_none()) return Err(MatchAccessError::ForeignKey());
            spec = ref<ArgSpec>::from_raw_parts(
                rstd::addressof((*global)->schema->args[(*global)->slot]));
            matched = ref<MatchedArg>::from_raw_parts(rstd::addressof((*global)->matched));
        }
        if (spec->type_id != any::TypeId::of<T>()) {
            return Err(MatchAccessError::WrongType());
        }
        if (matched->typed_values.is_empty()) return Ok(None());
        return Ok(Some(Values<T> { matched->typed_values.as_slice() }));
    }

    [[nodiscard]]
    auto contains(ref<str> id) const -> bool {
        auto slot = schema_->id_index.get(id);
        if (slot.is_some() && ! schema_->args[**slot].global) {
            return args_[**slot].occurrences != usize();
        }
        auto global = global_match(id);
        return global.is_some() && (*global)->matched.occurrences != usize();
    }

    [[nodiscard]]
    auto occurrences(ref<str> id) const -> usize {
        auto slot = schema_->id_index.get(id);
        if (slot.is_some() && ! schema_->args[**slot].global) return args_[**slot].occurrences;
        auto global = global_match(id);
        return global.is_some() ? (*global)->matched.occurrences : usize();
    }

    [[nodiscard]]
    auto value_source(ref<str> id) const -> Option<ValueSource> {
        auto            slot = schema_->id_index.get(id);
        ref<MatchedArg> matched;
        if (slot.is_some() && ! schema_->args[**slot].global) {
            matched = ref<MatchedArg>::from_raw_parts(rstd::addressof(args_[**slot]));
        } else {
            auto global = global_match(id);
            if (global.is_none()) return None();
            matched = ref<MatchedArg>::from_raw_parts(rstd::addressof((*global)->matched));
        }
        if (matched->occurrences == usize()) return None();
        return matched->from_default ? Some(ValueSource::DefaultValue())
                                     : Some(ValueSource::CommandLine());
    }

    [[nodiscard]]
    auto raw_values(ref<str> id) const -> Option<slice<OsString>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_some() && ! schema_->args[**slot].global) {
            return args_[**slot].raw_values.is_empty() ? None()
                                                       : Some(args_[**slot].raw_values.as_slice());
        }
        auto global = global_match(id);
        return global.is_none() || (*global)->matched.raw_values.is_empty()
                   ? None()
                   : Some((*global)->matched.raw_values.as_slice());
    }

    [[nodiscard]]
    auto indices(ref<str> id) const -> Option<slice<usize>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_some() && ! schema_->args[**slot].global) {
            return args_[**slot].indices.is_empty() ? None()
                                                    : Some(args_[**slot].indices.as_slice());
        }
        auto global = global_match(id);
        return global.is_none() || (*global)->matched.indices.is_empty()
                   ? None()
                   : Some((*global)->matched.indices.as_slice());
    }

    [[nodiscard]]
    auto occurrence_ends(ref<str> id) const -> Option<slice<usize>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_some() && ! schema_->args[**slot].global) {
            return args_[**slot].occurrence_ends.is_empty()
                       ? None()
                       : Some(args_[**slot].occurrence_ends.as_slice());
        }
        auto global = global_match(id);
        return global.is_none() || (*global)->matched.occurrence_ends.is_empty()
                   ? None()
                   : Some((*global)->matched.occurrence_ends.as_slice());
    }

    [[nodiscard]]
    auto subcommand() const -> Option<tuple<ref<str>, ref<Matches>>> {
        if (subcommand_name_.is_none() || subcommand_matches_.is_none()) return None();
        return Some(tuple<ref<str>, ref<Matches>>(subcommand_name_->as_str(),
                                                  subcommand_matches_->as_ref()));
    }

    [[nodiscard]]
    auto subcommand_matches(ref<str> name) const -> Option<ref<Matches>> {
        if (subcommand_name_.is_none() || subcommand_name_->as_str() != name ||
            subcommand_matches_.is_none()) {
            return None();
        }
        return Some(subcommand_matches_->as_ref());
    }

    [[nodiscard]]
    auto subcommand_matches(const CommandKey& key) const -> Option<ref<Matches>> {
        if (subcommand_matches_.is_none()) return None();
        auto child = subcommand_matches_->as_ref();
        if (child->schema_->command_token != key.command_) return None();
        return Some(child);
    }
};

class KnownMatches {
    Matches       matches_;
    Vec<OsString> unknown_;

    friend class Parser;
    KnownMatches(Matches matches, Vec<OsString> unknown)
        : matches_(rstd::move(matches)), unknown_(rstd::move(unknown)) {}

public:
    KnownMatches(const KnownMatches&)            = delete;
    KnownMatches& operator=(const KnownMatches&) = delete;
    KnownMatches(KnownMatches&&)                 = default;
    KnownMatches& operator=(KnownMatches&&)      = default;

    [[nodiscard]]
    auto matches() const noexcept -> ref<Matches> {
        return ref<Matches>::from_raw_parts(rstd::addressof(matches_));
    }
    [[nodiscard]]
    auto unknown() const noexcept -> slice<OsString> {
        return unknown_.as_slice();
    }
};

} // namespace rstd::argparse
