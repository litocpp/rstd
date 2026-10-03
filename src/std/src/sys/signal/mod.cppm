export module rstd:sys.signal;
import :io;

namespace rstd::sys::signal
{

inline constexpr int capacity = 64;

auto supported(int number) noexcept -> bool;
auto interrupt_number() noexcept -> int;
auto terminate_number() noexcept -> int;
auto hangup_number() noexcept -> int;
auto user1_number() noexcept -> int;
auto user2_number() noexcept -> int;
auto available() noexcept -> bool;
auto same_process() noexcept -> bool;
auto start(void (*dispatch)(int)) -> rstd::io::Result<empty>;
auto install(int number) -> rstd::io::Result<empty>;
auto restore(int number) -> rstd::io::Result<empty>;
auto sequence(int number) noexcept -> unsigned long long;

} // namespace rstd::sys::signal
