export module rstd.argparse:command_key;
export import rstd;

using namespace rstd::prelude;

export namespace rstd::argparse
{

class CommandKey final {
    u64 command_;

    constexpr explicit CommandKey(u64 command) noexcept: command_(command) {}

    friend class Command;
    friend class Matches;

public:
    constexpr CommandKey(const CommandKey&)            = default;
    constexpr CommandKey& operator=(const CommandKey&) = default;
};

} // namespace rstd::argparse
