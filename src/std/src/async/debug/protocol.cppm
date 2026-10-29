export module rstd:async.debug.protocol;
export import :async.debug;
import :time;

using namespace rstd;
using ::alloc::string::String;

export namespace rstd::async::debug::protocol
{
inline constexpr uint64_t VERSION = 2;
enum class Command
{
    Hello,
    Snapshot,
    SnapshotPage,
    Task,
    Events
};
struct Request {
    uint64_t         request_id {};
    Command          command { Command::Hello };
    uint64_t         argument {};
    Option<uint64_t> limit;
};
struct RequestError {
    ref<str>         message;
    Option<uint64_t> request_id;
};
struct Options {
    size_t         max_message_bytes { 65536 };
    size_t         max_snapshot_bytes { 4 * 1024 * 1024 };
    size_t         page_records { 32 };
    time::Duration snapshot_lifetime = time::Duration::from_secs(u64(30));
};
auto parse_request(slice<u8> line) -> Result<Request, RequestError>;
// An empty result means no complete response plus LF fits the budget.
auto error_response(RequestError error, size_t max_message_bytes) -> String;
class Session {
    const Handle&    debug_;
    Options          options_;
    Option<Snapshot> snapshot_;
    time::Instant    captured_ = time::Instant::now();
    uint64_t         snapshot_id_ {};
    size_t           page_ {};
    bool             hello_ {};
    auto             page(uint64_t request_id) -> String;

public:
    Session(const Handle& handle, Options options): debug_(handle), options_(options) {}
    auto respond(const Request& request) -> String;
};
} // namespace rstd::async::debug::protocol
