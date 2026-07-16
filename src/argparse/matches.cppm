module;
#include <rstd/enum.hpp>

export module rstd.argparse:matches;
import :schema;

using namespace rstd::prelude;
using rstd::ffi::OsString;
using rstd::sync::Arc;

struct MatchedArg {
    Vec<Box<dyn<rstd::any::Any>>> typed_values;
    Vec<OsString>                 raw_values;
    Vec<usize>                    indices;
    Vec<usize>                    occurrence_ends;
    usize                         occurrences { 0 };
    bool                          from_default { false };

    MatchedArg()
        : typed_values(Vec<Box<dyn<rstd::any::Any>>>::make()),
          raw_values(Vec<OsString>::make()),
          indices(Vec<usize>::make()),
          occurrence_ends(Vec<usize>::make()) {}
};

export namespace rstd::argparse
{

#define RSTD_ARGPARSE_VALUE_SOURCE_VARIANTS(V) \
    V(DefaultValue)                            \
    V(CommandLine)

RSTD_TAG_ENUM_WITH_DEFAULT(ValueSource, RSTD_ARGPARSE_VALUE_SOURCE_VARIANTS, CommandLine)

#undef RSTD_ARGPARSE_VALUE_SOURCE_VARIANTS

template<typename T>
class Values : public DefaultInClass<Values<T>, iter::Iterator> {
    slice<Box<dyn<any::Any>>> values_;
    usize                     next_ { 0 };

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
    Arc<CompiledCommand> schema_;
    Vec<MatchedArg>      args_;
    Option<String>       subcommand_name_;
    Option<Box<Matches>> subcommand_matches_;

    Matches(Arc<CompiledCommand> schema,
            Vec<MatchedArg>      args,
            Option<String>       subcommand_name,
            Option<Box<Matches>> subcommand_matches)
        : schema_(rstd::move(schema)),
          args_(rstd::move(args)),
          subcommand_name_(rstd::move(subcommand_name)),
          subcommand_matches_(rstd::move(subcommand_matches)) {}

    friend class Parser;

public:
    Matches(const Matches&)            = delete;
    Matches& operator=(const Matches&) = delete;
    Matches(Matches&&)                 = default;
    Matches& operator=(Matches&&)      = default;

    template<typename T>
    auto get_one(const ArgKey<T>& key) const -> Result<Option<ref<T>>, MatchAccessError> {
        if (key.command_ != schema_->command_token || key.slot_ >= schema_->args.len()) {
            return Err(MatchAccessError::ForeignKey());
        }
        const auto& spec = schema_->args[key.slot_];
        if (spec.type_id != any::TypeId::of<T>()) {
            return Err(MatchAccessError::WrongType());
        }
        if (spec.action.is_Append() ||
            (spec.num_args.maximum().is_none() || *spec.num_args.maximum() > 1)) {
            return Err(MatchAccessError::IncompatibleAccessor());
        }
        const auto& matched = args_[key.slot_];
        if (matched.typed_values.is_empty()) return Ok(None());
        auto value = any::downcast_ref<T>(matched.typed_values[0].as_ref());
        if (value.is_none()) return Err(MatchAccessError::WrongType());
        return Ok(rstd::move(value));
    }

    template<typename T>
    auto get_many(const ArgKey<T>& key) const -> Result<Option<Values<T>>, MatchAccessError> {
        if (key.command_ != schema_->command_token || key.slot_ >= schema_->args.len()) {
            return Err(MatchAccessError::ForeignKey());
        }
        const auto& spec = schema_->args[key.slot_];
        if (spec.type_id != any::TypeId::of<T>()) {
            return Err(MatchAccessError::WrongType());
        }
        const auto& matched = args_[key.slot_];
        if (matched.typed_values.is_empty()) return Ok(None());
        return Ok(Some(Values<T> { matched.typed_values.as_slice() }));
    }

    [[nodiscard]]
    auto contains(ref<str> id) const -> bool {
        auto slot = schema_->id_index.get(id);
        return slot.is_some() && args_[**slot].occurrences != 0;
    }

    [[nodiscard]]
    auto occurrences(ref<str> id) const -> usize {
        auto slot = schema_->id_index.get(id);
        return slot.is_some() ? args_[**slot].occurrences : 0;
    }

    [[nodiscard]]
    auto value_source(ref<str> id) const -> Option<ValueSource> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_none() || args_[**slot].occurrences == 0) return None();
        return args_[**slot].from_default ? Some(ValueSource::DefaultValue())
                                          : Some(ValueSource::CommandLine());
    }

    [[nodiscard]]
    auto raw_values(ref<str> id) const -> Option<slice<OsString>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_none() || args_[**slot].raw_values.is_empty()) return None();
        return Some(args_[**slot].raw_values.as_slice());
    }

    [[nodiscard]]
    auto indices(ref<str> id) const -> Option<slice<usize>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_none() || args_[**slot].indices.is_empty()) return None();
        return Some(args_[**slot].indices.as_slice());
    }

    [[nodiscard]]
    auto occurrence_ends(ref<str> id) const -> Option<slice<usize>> {
        auto slot = schema_->id_index.get(id);
        if (slot.is_none() || args_[**slot].occurrence_ends.is_empty()) return None();
        return Some(args_[**slot].occurrence_ends.as_slice());
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
