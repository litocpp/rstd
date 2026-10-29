export module rstd:async.debug;
export import rstd.core;
export import rstd.alloc;
import :sync.mutex;
import :time;
import :thread.join_handle;
import rstd.io;

using namespace rstd;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using ::alloc::sync::Arc;

namespace rstd::async::debug
{
export using Id     = rstd::uint64_t;
export using Cursor = rstd::uint64_t;
export enum class Kind { Runtime, Task, BlockingJob, Worker, Operation, Resource, Wait };
export enum class State {
    Created,
    Queued,
    Running,
    Waiting,
    FacilityQueued,
    FacilityRunning,
    Terminal,
    Cleaned,
    Published,
    Submitted,
    Ready,
    Consumed,
    ReleaseRequested,
    Released,
    Cancelled,
    Removed,
    Parked,
    Stopping,
    Closed
};
export enum class Error { Busy, NotFound, Evicted, HistoryGap, InvalidCursor, Capacity, Closed };
export enum class Outcome { Unknown, Success, Aborted, Error };
export struct RuntimeInfo {
    bool   multi_thread {}, io_enabled {}, time_enabled {};
    size_t workers {}, max_blocking_threads {};
};
export struct Options {
    size_t         max_active { 4096 };
    size_t         max_closed { 256 };
    size_t         max_events { 8192 };
    size_t         max_text_bytes { 512 };
    size_t         max_bytes { 16 * 1024 * 1024 };
    size_t         max_pending_events { 1024 };
    time::Duration aggregation_interval { time::Duration::from_millis(u64(100)) };
    auto           valid() const noexcept -> bool;
};
export struct TaskInfo {
    ref<str>        name {};
    source_location location {};
};
export struct Record {
    Id       id {}, parent {}, target {};
    Kind     kind { Kind::Task };
    State    state { State::Created };
    uint64_t version {}, changed_ns {};
    size_t   worker {};
    bool     external {}, cancel_requested {}, cancel_rejected {}, terminal {}, truncated {};
    bool     scheduler_terminal {}, frame_cleaned {}, result_published {}, backend_terminal {};
    Outcome  outcome { Outcome::Unknown };
    String   name = String::make();
    String   file = String::make();
    uint_least32_t line {};
    Record() = default;
    Record(const Record&);
    auto operator=(const Record&) -> Record&;
    Record(Record&&) noexcept                    = default;
    auto operator=(Record&&) noexcept -> Record& = default;
};
export struct Event {
    Cursor sequence {};
    Record record;
};
export struct Status {
    Id          session {};
    Cursor      cursor {};
    bool        closed {}, incomplete {}, exhausted {};
    uint64_t    omitted {}, dropped_events {};
    RuntimeInfo runtime;
    Options     limits;
};
export struct Snapshot : Status {
    uint64_t    observed_ns {};
    Vec<Record> records = Vec<Record>::make();
};
export struct Events : Status {
    Cursor     next_cursor {};
    Vec<Event> events = Vec<Event>::make();
};
struct CaptureShared;
struct QueryState;
class CollectionSession;
class Recorder;

export class Handle {
    Arc<QueryState> query_;
    explicit Handle(Arc<QueryState> query);
    friend class CollectionSession;

public:
    Handle(Handle&&) noexcept;
    auto operator=(Handle&&) noexcept -> Handle&;
    ~Handle();
    auto clone() const -> Handle;
    auto status() const -> Result<Status, Error>;
    /// The budget bounds retained record and text bytes before copying the snapshot.
    auto snapshot(size_t max_bytes = ~size_t(0)) const -> Result<Snapshot, Error>;
    auto task(Id id) const -> Result<Record, Error>;
    auto task_snapshot(Id id, size_t max_bytes = ~size_t(0)) const -> Result<Snapshot, Error>;
    auto events(Cursor after, size_t limit) const -> Result<Events, Error>;
};

// Tokens retain diagnostic storage, never runtime, frame, or native resource owners.
class Recorder {
    Option<Arc<CaptureShared>> capture_;
    Id                         id_ {};
    size_t                     slot_ {};
    uint64_t                   generation_ {};

    friend class CollectionSession;
    friend class WaitGuard;
    auto child_initial(Kind kind, TaskInfo info, Id target, bool waiting) const -> Recorder;

public:
    Recorder();
    Recorder(const Recorder&);
    auto operator=(const Recorder&) -> Recorder&;
    Recorder(Recorder&&) noexcept;
    auto operator=(Recorder&&) noexcept -> Recorder&;
    ~Recorder();
    static auto make(Options options, RuntimeInfo runtime = {}) -> Recorder;
    auto        child(Kind kind, TaskInfo info = {}, Id target = 0) const -> Recorder;
    auto        same_store(const Recorder& other) const -> bool;
    auto        without_parent() const -> Recorder;
    auto        id() const noexcept -> Id { return id_; }
    auto        target_id() const -> Id;
    auto        enabled() const noexcept -> bool { return capture_.is_some(); }
    void        state(State  state,
                      bool   terminal = false,
                      size_t worker   = ~size_t(0),
                      bool   external = false) const;
    void        cancel() const;
    void        cancel_rejected() const;
    void        outcome(Outcome value) const;
    void        close() const;
};

class CollectionSession {
    Arc<QueryState>                  query_;
    Recorder                         root_;
    Option<thread::JoinHandle<void>> thread_;
    CollectionSession(Arc<QueryState> query, Recorder root, thread::JoinHandle<void> thread);

public:
    CollectionSession(const CollectionSession&)                    = delete;
    auto operator=(const CollectionSession&) -> CollectionSession& = delete;
    CollectionSession(CollectionSession&&) noexcept;
    auto operator=(CollectionSession&&) noexcept -> CollectionSession&;
    ~CollectionSession();
    static auto start(const Recorder& root) -> rstd::io::Result<CollectionSession>;
    auto        handle() const -> Handle;
    void        close();
};
inline thread_local const Recorder* CURRENT = nullptr;
class Scope {
    const Recorder* previous_;

public:
    explicit Scope(const Recorder& current): previous_(CURRENT) { CURRENT = &current; }
    ~Scope() { CURRENT = previous_; }
    Scope(const Scope&) = delete;
};
auto current() -> Recorder;

export class WaitGuard {
    Recorder record_;

public:
    WaitGuard()                                    = default;
    WaitGuard(const WaitGuard&)                    = delete;
    auto operator=(const WaitGuard&) -> WaitGuard& = delete;
    WaitGuard(WaitGuard&& other) noexcept;
    auto operator=(WaitGuard&& other) noexcept -> WaitGuard&;
    ~WaitGuard();
    /// Call while executing the waiting task; destroying the guard removes the relation.
    void begin(ref<str> name, Id target = 0, source_location location = source_location::current());
    void reset();
};
export auto coverage() -> ref<str>;
} // namespace rstd::async::debug

namespace rstd
{
template<>
struct Impl<convert::From<async::debug::Kind>, ref<str>> {
    static auto from(async::debug::Kind value) -> ref<str>;
};
template<>
struct Impl<convert::From<async::debug::State>, ref<str>> {
    static auto from(async::debug::State value) -> ref<str>;
};
template<>
struct Impl<convert::From<async::debug::Error>, ref<str>> {
    static auto from(async::debug::Error value) -> ref<str>;
};
template<>
struct Impl<convert::From<async::debug::Outcome>, ref<str>> {
    static auto from(async::debug::Outcome value) -> ref<str>;
};
} // namespace rstd
