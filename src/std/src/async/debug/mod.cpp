module rstd;
import :async.debug;
import :sync.mutex;
import :sync.condvar;
import :sync.mpmc;
import :thread.builder;
import :time;

using namespace rstd;
using namespace rstd::literals;
using ::alloc::vec::Vec;
using ::alloc::string::String;
using ::alloc::sync::Arc;
using ::alloc::boxed::Box;

namespace rstd::async::debug
{
static uint64_t next_session = 1;
auto            now_ns() -> uint64_t {
    return time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
}
template<typename T>
auto atomic_read(const T& value) -> T {
    return __atomic_load_n(&value, __ATOMIC_ACQUIRE);
}
template<typename T>
void atomic_write(T& value, T next) {
    __atomic_store_n(&value, next, __ATOMIC_RELEASE);
}
auto increment(uint64_t& value) -> bool {
    auto old = atomic_read(value);
    while (old != ~uint64_t(0))
        if (__atomic_compare_exchange_n(
                &value, &old, old + 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    return false;
}
auto allocate_session() -> Id {
    auto value = atomic_read(next_session);
    while (value != ~Id(0))
        if (__atomic_compare_exchange_n(
                &next_session, &value, value + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return value;
    return 0;
}
Record::Record(const Record& r)
    : id(r.id),
      parent(r.parent),
      target(r.target),
      kind(r.kind),
      state(r.state),
      version(r.version),
      changed_ns(r.changed_ns),
      worker(r.worker),
      external(r.external),
      cancel_requested(r.cancel_requested),
      cancel_rejected(r.cancel_rejected),
      terminal(r.terminal),
      truncated(r.truncated),
      scheduler_terminal(r.scheduler_terminal),
      frame_cleaned(r.frame_cleaned),
      result_published(r.result_published),
      backend_terminal(r.backend_terminal),
      outcome(r.outcome),
      name(r.name.clone()),
      file(r.file.clone()),
      line(r.line) {
}
auto Record::operator=(const Record& r) -> Record& {
    if (this != &r) *this = Record(r);
    return *this;
}
struct CapturedState {
    Id       id {};
    uint64_t version {}, changed_ns {};
    State    state { State::Created };
    size_t   worker {};
    bool     external {}, cancel_requested {}, cancel_rejected {}, terminal {};
    bool     scheduler_terminal {}, frame_cleaned {}, result_published {}, backend_terminal {};
    Outcome  outcome { Outcome::Unknown };
    explicit CapturedState(const Record& r)
        : id(r.id),
          version(r.version),
          changed_ns(r.changed_ns),
          state(r.state),
          worker(r.worker),
          external(r.external),
          cancel_requested(r.cancel_requested),
          cancel_rejected(r.cancel_rejected),
          terminal(r.terminal),
          scheduler_terminal(r.scheduler_terminal),
          frame_cleaned(r.frame_cleaned),
          result_published(r.result_published),
          backend_terminal(r.backend_terminal),
          outcome(r.outcome) {}
    void apply(Record& r) const {
        r.version            = version;
        r.changed_ns         = changed_ns;
        r.state              = state;
        r.worker             = worker;
        r.external           = external;
        r.cancel_requested   = cancel_requested;
        r.cancel_rejected    = cancel_rejected;
        r.terminal           = terminal;
        r.scheduler_terminal = scheduler_terminal;
        r.frame_cleaned      = frame_cleaned;
        r.result_published   = result_published;
        r.backend_terminal   = backend_terminal;
        r.outcome            = outcome;
    }
};
struct CaptureEvent {
    size_t        slot;
    uint64_t      generation;
    CapturedState state;
};
struct SlotFields {
    bool     occupied {};
    uint64_t generation {}, last_queued {}, last_drained {};
    size_t   text_bytes {};
    Record   record;
};
struct CaptureSlot {
    sync::Mutex<SlotFields> fields { SlotFields {} };
};
struct Registration {
    Vec<usize> free     = Vec<usize>::make();
    Vec<usize> occupied = Vec<usize>::make();
    Id         next_id { 1 };
};
struct WakeState {
    bool stopped {}, notified {};
};
struct CaptureShared {
    Options                            options;
    RuntimeInfo                        runtime;
    Id                                 session;
    Vec<Box<CaptureSlot>>              slots;
    sync::Mutex<Registration>          registration { Registration {} };
    sync::mpmc::Sender<CaptureEvent>   sender;
    sync::mpmc::Receiver<CaptureEvent> receiver;
    sync::Mutex<WakeState>             wake { WakeState {} };
    sync::Condvar                      changed;
    size_t                             used_bytes {}, active {};
    uint64_t                           omitted {}, dropped_events {};
    bool                               closed {}, exhausted {}, flush_requested {};

    CaptureShared(Options                            options,
                  RuntimeInfo                        runtime,
                  Id                                 session,
                  sync::mpmc::Sender<CaptureEvent>   sender,
                  sync::mpmc::Receiver<CaptureEvent> receiver);
    auto reserve(size_t bytes) -> bool {
        auto used = atomic_read(used_bytes);
        while (bytes <= options.max_bytes - used) {
            if (__atomic_compare_exchange_n(
                    &used_bytes, &used, used + bytes, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return true;
        }
        return false;
    }
    void release(size_t bytes) { __atomic_fetch_sub(&used_bytes, bytes, __ATOMIC_ACQ_REL); }
    void lose_event() {
        if (! increment(dropped_events)) atomic_write(exhausted, true);
    }
    void omit() {
        if (! increment(omitted)) atomic_write(exhausted, true);
    }
    void notify() {
        if (__atomic_exchange_n(&flush_requested, true, __ATOMIC_ACQ_REL)) return;
        auto guard      = wake.lock().unwrap_unchecked();
        guard->notified = true;
        changed.notify_one();
    }
    void publish(size_t slot, SlotFields& fields) {
        auto& r = fields.record;
        if (r.version == ~uint64_t(0)) {
            atomic_write(exhausted, true);
            return;
        }
        ++r.version;
        r.changed_ns = now_ns();
        if (sender.try_send(CaptureEvent { slot, fields.generation, CapturedState(r) }).is_ok())
            fields.last_queued = r.version;
        else
            lose_event();
        if (sender.len().to_primitive() >=
            options.max_pending_events - options.max_pending_events / 4)
            notify();
    }
};
struct IndexEntry {
    Id     id;
    size_t slot;
};
struct QueryFields {
    Vec<Option<Record>>  records;
    Vec<IndexEntry>      index;
    Vec<usize>           scan;
    Option<CaptureEvent> pending;
    Vec<Option<Event>>   history;
    size_t               history_head {}, history_len {};
    Cursor               cursor {};
    uint64_t             seen_dropped {};
    Id                   next_id { 1 };
    QueryFields(size_t slots, size_t events)
        : records(Vec<Option<Record>>::with_capacity(usize(slots))),
          index(Vec<IndexEntry>::with_capacity(usize(slots))),
          scan(Vec<usize>::with_capacity(usize(slots))),
          history(Vec<Option<Event>>::with_capacity(usize(events))) {
        records.resize_with(usize(slots), [] {
            return Option<Record> {};
        });
        history.resize_with(usize(events), [] {
            return Option<Event> {};
        });
    }
};
struct QueryState {
    Arc<CaptureShared>       capture;
    sync::Mutex<QueryFields> fields;
    explicit QueryState(Arc<CaptureShared> capture)
        : capture(rstd::move(capture)),
          fields(this->capture->slots.len().to_primitive(), this->capture->options.max_events) {}
    ~QueryState();
    auto refresh(QueryFields& fields, bool nonblocking) -> bool;
};
auto checked_add(size_t& value, size_t amount, size_t count = 1) -> bool {
    if (count && amount > (~size_t(0) - value) / count) return false;
    value += amount * count;
    return true;
}
auto fixed_cost(const Options& o) -> Option<size_t> {
    size_t count = o.max_active;
    if (! checked_add(count, o.max_closed)) return None();
    size_t bytes = sizeof(CaptureShared) + sizeof(QueryState) + sizeof(QueryFields) +
                   sizeof(CollectionSession) + 4096;
    if (! checked_add(bytes,
                      sizeof(CaptureSlot) + sizeof(Box<CaptureSlot>) + sizeof(Option<Record>) +
                          sizeof(IndexEntry) + 3 * sizeof(usize) + 64,
                      count) ||
        ! checked_add(bytes, sizeof(Option<Event>), o.max_events) ||
        ! checked_add(bytes, sizeof(CaptureEvent) + 64, o.max_pending_events))
        return None();
    return Some(bytes);
}
auto Options::valid() const noexcept -> bool {
    if (! max_active || ! max_closed || ! max_events || ! max_pending_events || ! max_text_bytes ||
        max_text_bytes > 65536 || aggregation_interval == time::Duration {})
        return false;
    auto fixed = fixed_cost(*this);
    return fixed.is_some() && *fixed <= max_bytes && max_text_bytes <= (max_bytes - *fixed) / 4;
}
CaptureShared::CaptureShared(Options                            options,
                             RuntimeInfo                        runtime,
                             Id                                 session,
                             sync::mpmc::Sender<CaptureEvent>   sender,
                             sync::mpmc::Receiver<CaptureEvent> receiver)
    : options(options),
      runtime(runtime),
      session(session),
      slots(Vec<Box<CaptureSlot>>::with_capacity(usize(options.max_active + options.max_closed))),
      sender(rstd::move(sender)),
      receiver(rstd::move(receiver)),
      used_bytes(fixed_cost(options).unwrap_unchecked()) {
    exhausted       = session == 0;
    auto registered = registration.lock().unwrap_unchecked();
    registered->free.reserve_exact(usize(options.max_active + options.max_closed));
    registered->occupied.reserve_exact(usize(options.max_active + options.max_closed));
    for (size_t i = 0; i < options.max_active + options.max_closed; ++i) {
        slots.push(Box<CaptureSlot>::make());
        registered->free.push(usize(options.max_active + options.max_closed - i - 1));
    }
}
auto text_cost(const Record& r) -> size_t {
    return r.name.capacity().to_primitive() + r.file.capacity().to_primitive();
}
auto cost(const Record& r) -> size_t {
    return sizeof(Record) + text_cost(r);
}
auto bounded_text(ref<str> s, size_t maximum, bool& truncated) -> String {
    auto n = s.len().to_primitive();
    if (n > maximum) {
        n = maximum;
        while (n && (s[usize(n)].to_primitive() & 0xc0) == 0x80) --n;
        truncated = true;
    }
    auto bytes = Vec<u8>::with_capacity(usize(n));
    for (size_t i = 0; i < n; ++i) bytes.push(u8(s[usize(i)].to_primitive()));
    return String::from_utf8_unchecked(rstd::move(bytes));
}
auto c_text(const char* p) -> ref<str> {
    size_t n = 0;
    while (p[n]) ++n;
    return ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(p), usize(n));
}
auto find_index(const QueryFields& f, Id id) -> size_t {
    size_t first = 0, last = f.index.len().to_primitive();
    while (first < last) {
        auto middle = first + (last - first) / 2;
        if (f.index[usize(middle)].id < id)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}
void evict_event(CaptureShared& c, QueryFields& f) {
    auto& event = f.history[usize(f.history_head)];
    auto  bytes = text_cost(event->record);
    event       = None();
    c.release(bytes);
    f.history_head = (f.history_head + 1) % c.options.max_events;
    --f.history_len;
}
auto next_cursor(CaptureShared& c, QueryFields& f) -> Cursor {
    if (f.cursor == ~Cursor(0)) {
        atomic_write(c.exhausted, true);
        return 0;
    }
    return ++f.cursor;
}
void publish_event(CaptureShared&       c,
                   QueryFields&         f,
                   const Record&        metadata,
                   const CapturedState& state) {
    auto sequence = next_cursor(c, f);
    if (! sequence) return;
    auto bytes = metadata.name.len().to_primitive() + metadata.file.len().to_primitive();
    if (f.history_len == c.options.max_events) evict_event(c, f);
    while (! c.reserve(bytes)) {
        if (! f.history_len) return;
        evict_event(c, f);
    }
    auto record = Record(metadata);
    state.apply(record);
    auto index              = (f.history_head + f.history_len) % c.options.max_events;
    f.history[usize(index)] = Some(Event { sequence, rstd::move(record) });
    ++f.history_len;
}
void update_record(QueryFields& f, size_t slot, const Record& record) {
    auto& value = f.records[usize(slot)];
    if (value.is_none()) {
        value      = Some(Record(record));
        auto index = find_index(f, record.id);
        f.index.insert(usize(index), IndexEntry { record.id, slot });
    } else if (value->id == record.id && value->version < record.version) {
        CapturedState(record).apply(*value);
    }
}
template<typename T>
auto acquire(sync::Mutex<T>& mutex, bool nonblocking) -> Option<sync::MutexGuard<T>> {
    if (! nonblocking) return Some(mutex.lock().unwrap_unchecked());
    auto result = mutex.try_lock();
    if (result.is_err()) return None();
    return Some(rstd::move(result).unwrap_unchecked());
}
auto QueryState::refresh(QueryFields& f, bool nonblocking) -> bool {
    auto& c = *capture;
    {
        auto registration = acquire(c.registration, nonblocking);
        if (! registration) return false;
        f.next_id = (*registration)->next_id;
        f.scan.clear();
        for (auto slot : (*registration)->occupied) f.scan.push(rstd::move(slot));
    }
    auto batch = c.options.max_pending_events + (f.pending.is_some() ? 1 : 0);
    for (size_t i = 0; i < batch; ++i) {
        if (! f.pending) {
            auto incoming = c.receiver.try_recv();
            if (incoming.is_err()) break;
            f.pending = Some(rstd::move(incoming).unwrap_unchecked());
        }
        const auto& event       = *f.pending;
        auto        entry_guard = acquire(c.slots[usize(event.slot)]->fields, nonblocking);
        if (! entry_guard) return false;
        auto& entry = **entry_guard;
        if (! entry.occupied || entry.generation != event.generation ||
            entry.record.id != event.state.id) {
            (void)next_cursor(c, f);
        } else {
            publish_event(c, f, entry.record, event.state);
            update_record(f, event.slot, entry.record);
            if (event.state.version > entry.last_drained) entry.last_drained = event.state.version;
            if (entry.record.id >= f.next_id) f.next_id = entry.record.id + 1;
        }
        f.pending = None();
    }
    auto dropped = atomic_read(c.dropped_events);
    if (dropped != f.seen_dropped) {
        (void)next_cursor(c, f);
        f.seen_dropped = dropped;
    }
    for (auto slot : f.scan) {
        auto entry_guard = acquire(c.slots[slot]->fields, nonblocking);
        if (! entry_guard) return false;
        auto& entry = **entry_guard;
        if (entry.occupied) update_record(f, slot.to_primitive(), entry.record);
    }
    size_t closed = 0;
    for (const auto& indexed : f.index) closed += f.records[usize(indexed.slot)]->terminal;
    while (closed > c.options.max_closed) {
        auto selected = c.slots.len().to_primitive();
        for (const auto& indexed : f.index) {
            const auto& value = f.records[usize(indexed.slot)];
            if (! value->terminal) continue;
            auto entry_guard = acquire(c.slots[usize(indexed.slot)]->fields, nonblocking);
            if (! entry_guard) return false;
            const auto& entry = **entry_guard;
            if (entry.last_drained >= entry.last_queued) {
                selected = indexed.slot;
                break;
            }
        }
        if (selected == c.slots.len().to_primitive()) break;
        // Registration and reclamation both acquire the registry before the slot.
        auto registration = acquire(c.registration, nonblocking);
        if (! registration) return false;
        auto entry_guard = acquire(c.slots[usize(selected)]->fields, nonblocking);
        if (! entry_guard) return false;
        auto& entry = **entry_guard;
        auto  at    = find_index(f, entry.record.id);
        f.index.remove(usize(at));
        f.records[usize(selected)] = None();
        auto bytes                 = entry.text_bytes * 2;
        entry.record               = Record {};
        entry.occupied             = false;
        entry.text_bytes           = 0;
        c.release(bytes);
        auto& occupied = (*registration)->occupied;
        for (usize i {}; i < occupied.len(); ++i)
            if (occupied[i] == usize(selected)) {
                auto last = occupied.pop().unwrap_unchecked();
                if (i < occupied.len()) occupied[i] = last;
                break;
            }
        (*registration)->free.push(usize(selected));
        --closed;
    }
    return true;
}
QueryState::~QueryState() {
    auto f = fields.lock().unwrap_unchecked();
    while (f->history_len) evict_event(*capture, *f);
    // Registration reserves the metadata copy even when no query has materialized it yet.
    auto registered = capture->registration.lock().unwrap_unchecked();
    for (auto slot : registered->occupied) {
        auto entry       = capture->slots[slot]->fields.lock().unwrap_unchecked();
        f->records[slot] = None();
        capture->release(entry->text_bytes);
    }
}
Recorder::Recorder()                                       = default;
Recorder::~Recorder()                                      = default;
Recorder::Recorder(Recorder&&) noexcept                    = default;
auto Recorder::operator=(Recorder&&) noexcept -> Recorder& = default;
Recorder::Recorder(const Recorder& r): id_(r.id_), slot_(r.slot_), generation_(r.generation_) {
    if (r.capture_) capture_ = Some(r.capture_->clone());
}
auto Recorder::operator=(const Recorder& r) -> Recorder& {
    if (this != &r) *this = Recorder(r);
    return *this;
}
auto Recorder::make(Options o, RuntimeInfo runtime) -> Recorder {
    auto r       = Recorder {};
    auto session = allocate_session();
    if (! o.valid()) return r;
    auto [sender, receiver] = sync::mpmc::sync_channel<CaptureEvent>(usize(o.max_pending_events));
    r.capture_              = Some(
        Arc<CaptureShared>::make(o, runtime, session, rstd::move(sender), rstd::move(receiver)));
    auto root = r.child(Kind::Runtime, { "runtime"_str });
    if (! root.enabled()) return r;
    root.state(State::Running);
    return root;
}
auto Recorder::same_store(const Recorder& r) const -> bool {
    return capture_ && r.capture_ && Arc<CaptureShared>::ptr_eq(*capture_, *r.capture_);
}
auto Recorder::without_parent() const -> Recorder {
    auto value = *this;
    value.id_  = 0;
    return value;
}
auto Recorder::child(Kind kind, TaskInfo info, Id target) const -> Recorder {
    return child_initial(kind, info, target, false);
}
auto Recorder::child_initial(Kind kind, TaskInfo info, Id target, bool waiting) const -> Recorder {
    if (! capture_) return {};
    auto& c            = **capture_;
    auto  registration = c.registration.lock().unwrap_unchecked();
    if (atomic_read(c.closed) || atomic_read(c.exhausted)) return {};
    if (registration->next_id == ~Id(0)) {
        atomic_write(c.exhausted, true);
        return {};
    }
    if (atomic_read(c.active) >= c.options.max_active || registration->free.is_empty()) {
        c.omit();
        return {};
    }
    auto file       = c_text(info.location.file_name());
    auto name_bytes = rstd::min(info.name.len().to_primitive(), c.options.max_text_bytes);
    auto file_bytes = rstd::min(file.len().to_primitive(), c.options.max_text_bytes);
    auto reserved   = (name_bytes + file_bytes) * 2;
    if (! c.reserve(reserved)) {
        c.omit();
        return {};
    }
    auto slot  = registration->free.pop().unwrap_unchecked().to_primitive();
    auto entry = c.slots[usize(slot)]->fields.lock().unwrap_unchecked();
    if (entry->generation == ~uint64_t(0)) {
        c.release(reserved);
        atomic_write(c.exhausted, true);
        return {};
    }
    ++entry->generation;
    auto& r           = entry->record;
    r                 = Record {};
    r.id              = registration->next_id++;
    r.kind            = kind;
    r.parent          = id_;
    r.target          = target;
    r.line            = info.location.line();
    r.name            = bounded_text(info.name, c.options.max_text_bytes, r.truncated);
    r.file            = bounded_text(file, c.options.max_text_bytes, r.truncated);
    entry->text_bytes = text_cost(r);
    c.release(reserved - entry->text_bytes * 2);
    entry->occupied = true;
    registration->occupied.push(usize(slot));
    entry->last_queued = entry->last_drained = 0;
    __atomic_fetch_add(&c.active, size_t(1), __ATOMIC_ACQ_REL);
    c.publish(slot, *entry);
    if (waiting) {
        r.state = State::Waiting;
        c.publish(slot, *entry);
    }
    auto result        = Recorder(*this);
    result.id_         = r.id;
    result.slot_       = slot;
    result.generation_ = entry->generation;
    return result;
}
void Recorder::state(State state, bool terminal, size_t worker, bool external) const {
    if (! capture_ || ! id_) return;
    auto& c     = **capture_;
    auto  entry = c.slots[usize(slot_)]->fields.lock().unwrap_unchecked();
    if (! entry->occupied || entry->generation != generation_ || entry->record.id != id_) return;
    auto& r = entry->record;
    if (r.terminal || atomic_read(c.exhausted)) return;
    if (worker == ~size_t(0)) worker = r.worker;
    if (r.state == state && r.worker == worker && r.external == external && ! terminal) return;
    if ((r.frame_cleaned || r.scheduler_terminal) &&
        (state == State::Created || state == State::Queued || state == State::Running ||
         state == State::Waiting || state == State::FacilityQueued ||
         state == State::FacilityRunning || state == State::Terminal))
        return;
    r.scheduler_terminal |= state == State::Terminal;
    r.frame_cleaned |= state == State::Cleaned;
    r.result_published |= state == State::Published;
    r.backend_terminal |= state == State::Ready;
    r.state    = state;
    r.worker   = worker;
    r.external = external;
    r.terminal = terminal;
    if (terminal) __atomic_fetch_sub(&c.active, size_t(1), __ATOMIC_ACQ_REL);
    if (r.kind == Kind::Runtime && state == State::Closed) atomic_write(c.closed, true);
    c.publish(slot_, *entry);
}
template<typename Update>
void update_token(const Option<Arc<CaptureShared>>& capture,
                  size_t                            slot,
                  Id                                id,
                  uint64_t                          generation,
                  Update                            update) {
    if (! capture || ! id) return;
    auto& c     = **capture;
    auto  entry = c.slots[usize(slot)]->fields.lock().unwrap_unchecked();
    if (! entry->occupied || entry->generation != generation || entry->record.id != id ||
        entry->record.terminal || atomic_read(c.exhausted))
        return;
    if (update(entry->record)) c.publish(slot, *entry);
}
void Recorder::cancel() const {
    update_token(capture_, slot_, id_, generation_, [](Record& r) {
        return ! rstd::exchange(r.cancel_requested, true);
    });
}
void Recorder::cancel_rejected() const {
    update_token(capture_, slot_, id_, generation_, [](Record& r) {
        return ! rstd::exchange(r.cancel_rejected, true);
    });
}
void Recorder::outcome(Outcome value) const {
    update_token(capture_, slot_, id_, generation_, [value](Record& r) {
        return rstd::exchange(r.outcome, value) != value;
    });
}
void Recorder::close() const {
    state(State::Closed, true);
    if (capture_) atomic_write((**capture_).closed, true);
}
auto current() -> Recorder {
    return CURRENT ? Recorder(*CURRENT) : Recorder {};
}
auto Recorder::target_id() const -> Id {
    return CURRENT && same_store(*CURRENT) ? id_ : 0;
}
Handle::Handle(Arc<QueryState> query): query_(rstd::move(query)) {
}
Handle::Handle(Handle&&) noexcept                    = default;
auto Handle::operator=(Handle&&) noexcept -> Handle& = default;
Handle::~Handle()                                    = default;
auto Handle::clone() const -> Handle {
    return Handle(query_.clone());
}
auto status_from(const CaptureShared& c, const QueryFields& f) -> Status {
    Status status;
    status.session        = c.session;
    status.cursor         = f.cursor;
    status.closed         = atomic_read(c.closed);
    status.exhausted      = atomic_read(c.exhausted);
    status.omitted        = atomic_read(c.omitted);
    status.dropped_events = atomic_read(c.dropped_events);
    status.incomplete     = status.omitted != 0 || status.dropped_events != 0;
    status.runtime        = c.runtime;
    status.limits         = c.options;
    return status;
}
auto query_lock(const QueryState& query) -> Result<sync::MutexGuard<QueryFields>, Error> {
    auto locked = query.fields.try_lock();
    if (locked.is_err()) return Err(Error::Busy);
    auto fields = rstd::move(locked).unwrap_unchecked();
    if (! const_cast<QueryState&>(query).refresh(*fields, true)) return Err(Error::Busy);
    return Ok(rstd::move(fields));
}
auto Handle::status() const -> Result<Status, Error> {
    auto result = query_lock(*query_);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    return Ok(status_from(*query_->capture, *result.unwrap_unchecked()));
}
template<typename Matches>
auto make_snapshot(const QueryState& query, const QueryFields& f, size_t max_bytes, Matches matches)
    -> Result<Snapshot, Error> {
    size_t bytes = 0, count = 0;
    for (const auto& indexed : f.index) {
        const auto& item = f.records[usize(indexed.slot)];
        if (! matches(*item)) continue;
        if (cost(*item) > max_bytes - bytes) return Err(Error::Capacity);
        bytes += cost(*item);
        ++count;
    }
    Snapshot s;
    static_cast<Status&>(s) = status_from(*query.capture, f);
    s.observed_ns           = now_ns();
    s.records.reserve_exact(usize(count));
    for (const auto& indexed : f.index) {
        const auto& item = f.records[usize(indexed.slot)];
        if (matches(*item)) s.records.push(Record(*item));
    }
    return Ok(rstd::move(s));
}
auto Handle::snapshot(size_t max_bytes) const -> Result<Snapshot, Error> {
    auto result = query_lock(*query_);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    return make_snapshot(*query_, *result.unwrap_unchecked(), max_bytes, [](const Record&) {
        return true;
    });
}
auto get_task(const QueryFields& f, Id id) -> const Record* {
    auto index = find_index(f, id);
    if (index == f.index.len().to_primitive() || f.index[usize(index)].id != id) return nullptr;
    return rstd::addressof(*f.records[usize(f.index[usize(index)].slot)]);
}
auto Handle::task(Id id) const -> Result<Record, Error> {
    auto result = query_lock(*query_);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    auto  f = rstd::move(result).unwrap_unchecked();
    auto* r = get_task(*f, id);
    if (! r) return Err(id && id < f->next_id ? Error::Evicted : Error::NotFound);
    if (r->kind != Kind::Task && r->kind != Kind::BlockingJob) return Err(Error::NotFound);
    return Ok(Record(*r));
}
auto Handle::task_snapshot(Id id, size_t max_bytes) const -> Result<Snapshot, Error> {
    auto result = query_lock(*query_);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    auto  f = rstd::move(result).unwrap_unchecked();
    auto* r = get_task(*f, id);
    if (! r) return Err(id && id < f->next_id ? Error::Evicted : Error::NotFound);
    if (r->kind != Kind::Task && r->kind != Kind::BlockingJob) return Err(Error::NotFound);
    return make_snapshot(*query_, *f, max_bytes, [id](const Record& r) {
        return r.id == id || r.parent == id || (r.kind == Kind::Wait && r.target == id);
    });
}
auto Handle::events(Cursor after, size_t limit) const -> Result<Events, Error> {
    auto result = query_lock(*query_);
    if (result.is_err()) return Err(result.unwrap_err_unchecked());
    auto f = rstd::move(result).unwrap_unchecked();
    if (after > f->cursor || ! limit) return Err(Error::InvalidCursor);
    Events out;
    static_cast<Status&>(out) = status_from(*query_->capture, *f);
    out.next_cursor           = after;
    for (size_t i = 0; i < f->history_len; ++i) {
        const auto& e =
            *f->history[usize((f->history_head + i) % query_->capture->options.max_events)];
        if (e.sequence <= after) continue;
        if (e.sequence != out.next_cursor + 1) return Err(Error::HistoryGap);
        out.events.push(Event { e.sequence, Record(e.record) });
        out.next_cursor = e.sequence;
        if (out.events.len().to_primitive() >= limit) break;
    }
    if (out.events.len().to_primitive() < limit && out.next_cursor < f->cursor)
        return Err(Error::HistoryGap);
    return Ok(rstd::move(out));
}
CollectionSession::CollectionSession(Arc<QueryState>          query,
                                     Recorder                 root,
                                     thread::JoinHandle<void> thread)
    : query_(rstd::move(query)), root_(rstd::move(root)), thread_(Some(rstd::move(thread))) {
}
CollectionSession::CollectionSession(CollectionSession&& other) noexcept
    : query_(rstd::move(other.query_)),
      root_(rstd::move(other.root_)),
      thread_(other.thread_.take()) {
}
auto CollectionSession::operator=(CollectionSession&& other) noexcept -> CollectionSession& {
    if (this != &other) {
        close();
        query_  = rstd::move(other.query_);
        root_   = rstd::move(other.root_);
        thread_ = other.thread_.take();
    }
    return *this;
}
CollectionSession::~CollectionSession() {
    close();
}
auto CollectionSession::start(const Recorder& root) -> rstd::io::Result<CollectionSession> {
    if (! root.capture_)
        return Err(
            rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::InvalidInput }));
    auto query  = Arc<QueryState>::make(root.capture_->clone());
    auto worker = thread::builder::Builder::make().spawn([query = query.clone()] {
        auto& c = *query->capture;
        while (true) {
            bool stopped;
            {
                auto wake = c.wake.lock().unwrap_unchecked();
                if (! wake->stopped && ! wake->notified)
                    (void)c.changed.wait_timeout(wake, c.options.aggregation_interval);
                stopped        = wake->stopped;
                wake->notified = false;
                atomic_write(c.flush_requested, false);
            }
            {
                auto fields = query->fields.lock().unwrap_unchecked();
                (void)query->refresh(*fields, false);
            }
            if (stopped) break;
        }
    });
    if (worker.is_err()) return Err(rstd::move(worker).unwrap_err_unchecked());
    return Ok(CollectionSession(
        rstd::move(query), Recorder(root), rstd::move(worker).unwrap_unchecked()));
}
auto CollectionSession::handle() const -> Handle {
    return Handle(query_.clone());
}
void CollectionSession::close() {
    if (! thread_) return;
    root_.close();
    {
        auto wake      = query_->capture->wake.lock().unwrap_unchecked();
        wake->stopped  = true;
        wake->notified = true;
        query_->capture->changed.notify_all();
    }
    (void)rstd::move(thread_.take().unwrap_unchecked()).join();
    auto fields = query_->fields.lock().unwrap_unchecked();
    (void)query_->refresh(*fields, false);
}
WaitGuard::WaitGuard(WaitGuard&& other) noexcept: record_(rstd::move(other.record_)) {
    other.record_ = {};
}
auto WaitGuard::operator=(WaitGuard&& other) noexcept -> WaitGuard& {
    if (this != &other) {
        reset();
        record_       = rstd::move(other.record_);
        other.record_ = {};
    }
    return *this;
}
WaitGuard::~WaitGuard() {
    reset();
}
void WaitGuard::begin(ref<str> name, Id target, source_location location) {
    if (record_.id() || ! CURRENT || ! CURRENT->enabled()) return;
    record_ = CURRENT->child_initial(Kind::Wait, TaskInfo { name, location }, target, true);
}
void WaitGuard::reset() {
    record_.state(State::Removed, true);
    record_ = {};
}
auto coverage() -> ref<str> {
    return "tasks,blocking_jobs,workers,timers,join,owned_io,source_release; unknown:mutex,notify,oneshot,readiness,third_party; await target=0 means unknown; no native stacks"_str;
}
} // namespace rstd::async::debug

namespace rstd
{
auto Impl<convert::From<async::debug::Kind>, ref<str>>::from(async::debug::Kind value) -> ref<str> {
    using async::debug::Kind;
    switch (value) {
    case Kind::Runtime: return "runtime"_str;
    case Kind::Task: return "task"_str;
    case Kind::BlockingJob: return "blocking_job"_str;
    case Kind::Worker: return "worker"_str;
    case Kind::Operation: return "operation"_str;
    case Kind::Resource: return "resource"_str;
    case Kind::Wait: return "wait"_str;
    }
    return "unknown"_str;
}
auto Impl<convert::From<async::debug::State>, ref<str>>::from(async::debug::State value)
    -> ref<str> {
    using async::debug::State;
    switch (value) {
    case State::Created: return "created"_str;
    case State::Queued: return "queued"_str;
    case State::Running: return "running"_str;
    case State::Waiting: return "waiting"_str;
    case State::FacilityQueued: return "facility_queued"_str;
    case State::FacilityRunning: return "facility_running"_str;
    case State::Terminal: return "terminal"_str;
    case State::Cleaned: return "cleaned"_str;
    case State::Published: return "published"_str;
    case State::Submitted: return "submitted"_str;
    case State::Ready: return "ready"_str;
    case State::Consumed: return "consumed"_str;
    case State::ReleaseRequested: return "release_requested"_str;
    case State::Released: return "released"_str;
    case State::Cancelled: return "cancelled"_str;
    case State::Removed: return "removed"_str;
    case State::Parked: return "parked"_str;
    case State::Stopping: return "stopping"_str;
    case State::Closed: return "closed"_str;
    }
    return "unknown"_str;
}
auto Impl<convert::From<async::debug::Outcome>, ref<str>>::from(async::debug::Outcome value)
    -> ref<str> {
    using async::debug::Outcome;
    switch (value) {
    case Outcome::Unknown: return "unknown"_str;
    case Outcome::Success: return "success"_str;
    case Outcome::Aborted: return "aborted"_str;
    case Outcome::Error: return "error"_str;
    }
    return "unknown"_str;
}
auto Impl<convert::From<async::debug::Error>, ref<str>>::from(async::debug::Error value)
    -> ref<str> {
    using async::debug::Error;
    switch (value) {
    case Error::Busy: return "busy"_str;
    case Error::NotFound: return "not_found"_str;
    case Error::Evicted: return "evicted"_str;
    case Error::HistoryGap: return "history_gap"_str;
    case Error::Capacity: return "snapshot_too_large"_str;
    case Error::InvalidCursor: return "invalid_cursor"_str;
    case Error::Closed: return "runtime_closed"_str;
    }
    return "unknown"_str;
}
} // namespace rstd
