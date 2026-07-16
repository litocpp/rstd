export module rstd.argparse:schema;
import :arg;

using namespace rstd::prelude;
using rstd::collections::BTreeMap;
using rstd::sync::Arc;

struct CompiledGroup {
    String     id;
    Vec<usize> members;
    bool       required;
    bool       multiple;
};

struct CompiledConflict {
    usize left;
    usize right;
};

struct CompiledRequirement {
    usize source;
    bool  target_is_group;
    usize target;
};

struct CompiledCommand;

struct CompiledSubcommand {
    String               name;
    Vec<String>          aliases;
    Arc<CompiledCommand> schema;
};

struct CompiledCommand {
    String                   name;
    String                   command_path;
    Option<String>           about;
    Option<String>           long_about;
    Option<String>           version;
    Option<String>           after_help;
    Vec<ArgSpec>             args;
    BTreeMap<String, usize>  id_index;
    BTreeMap<String, usize>  option_index;
    Vec<usize>               positionals;
    Vec<usize>               positional_reserve;
    Vec<CompiledGroup>       groups;
    Vec<CompiledConflict>    conflicts;
    Vec<CompiledRequirement> requirements;
    Vec<CompiledSubcommand>  subcommands;
    BTreeMap<String, usize>  subcommand_index;
    bool                     subcommand_required;
    u64                      command_token;

    CompiledCommand(String                   name,
                    Option<String>           about,
                    Option<String>           long_about,
                    Option<String>           version,
                    Option<String>           after_help,
                    Vec<ArgSpec>             args,
                    BTreeMap<String, usize>  id_index,
                    BTreeMap<String, usize>  option_index,
                    Vec<usize>               positionals,
                    Vec<usize>               positional_reserve,
                    Vec<CompiledGroup>       groups,
                    Vec<CompiledConflict>    conflicts,
                    Vec<CompiledRequirement> requirements,
                    Vec<CompiledSubcommand>  subcommands,
                    BTreeMap<String, usize>  subcommand_index,
                    bool                     subcommand_required,
                    u64                      command_token)
        : name(rstd::move(name)),
          command_path(this->name.clone()),
          about(rstd::move(about)),
          long_about(rstd::move(long_about)),
          version(rstd::move(version)),
          after_help(rstd::move(after_help)),
          args(rstd::move(args)),
          id_index(rstd::move(id_index)),
          option_index(rstd::move(option_index)),
          positionals(rstd::move(positionals)),
          positional_reserve(rstd::move(positional_reserve)),
          groups(rstd::move(groups)),
          conflicts(rstd::move(conflicts)),
          requirements(rstd::move(requirements)),
          subcommands(rstd::move(subcommands)),
          subcommand_index(rstd::move(subcommand_index)),
          subcommand_required(subcommand_required),
          command_token(command_token) {}
};
