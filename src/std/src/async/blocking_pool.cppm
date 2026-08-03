export module rstd:async.blocking_pool;
import :io;
import :sync;
import :thread;
import :time;
import rstd.alloc;
import rstd.core;

using ::alloc::boxed::Box;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::sync::Arc;
using rstd::sync::Weak;

namespace rstd::async::blocking_pool
{

enum class JobLifecycle
{
    Queued,
    Running,
    Cancelled,
    Completed,
};

using JobFn = Box<dyn<FnMut<void()>>>;

struct JobFields {
    JobLifecycle  lifecycle { JobLifecycle::Queued };
    Option<JobFn> run;
    Option<JobFn> cancel;
};

struct JobState {
    sync::Mutex<JobFields> fields;

    JobState(JobFn run, JobFn cancel)
        : fields(
              JobFields { JobLifecycle::Queued, Some(rstd::move(run)), Some(rstd::move(cancel)) }) {
    }

    void run() {
        auto function = Option<JobFn> {};
        {
            auto state = fields.lock().unwrap_unchecked();
            if (state->lifecycle != JobLifecycle::Queued) return;
            state->lifecycle = JobLifecycle::Running;
            function         = state->run.take();
            state->cancel    = None<JobFn>();
        }

        rstd::move(function).unwrap_unchecked()->operator()();

        auto state       = fields.lock().unwrap_unchecked();
        state->lifecycle = JobLifecycle::Completed;
    }

    auto cancel_queued() -> bool {
        auto function = Option<JobFn> {};
        {
            auto state = fields.lock().unwrap_unchecked();
            if (state->lifecycle != JobLifecycle::Queued) return false;
            state->lifecycle = JobLifecycle::Cancelled;
            state->run       = None<JobFn>();
            function         = state->cancel.take();
        }
        rstd::move(function).unwrap_unchecked()->operator()();
        return true;
    }
};

} // namespace rstd::async::blocking_pool

namespace rstd::async
{

class BlockingJobCancellation {
    Weak<blocking_pool::JobState> m_state;

    explicit BlockingJobCancellation(Weak<blocking_pool::JobState> state)
        : m_state(rstd::move(state)) {}

    friend class BlockingJob;

public:
    BlockingJobCancellation()                                                      = delete;
    BlockingJobCancellation(const BlockingJobCancellation&)                        = delete;
    auto operator=(const BlockingJobCancellation&) -> BlockingJobCancellation&     = delete;
    BlockingJobCancellation(BlockingJobCancellation&&) noexcept                    = default;
    auto operator=(BlockingJobCancellation&&) noexcept -> BlockingJobCancellation& = default;

    auto clone() const -> BlockingJobCancellation {
        return BlockingJobCancellation { m_state.clone() };
    }

    auto cancel() const -> bool {
        auto state = m_state.upgrade();
        return state && state->cancel_queued();
    }
};

class BlockingJob {
    Arc<blocking_pool::JobState> m_state;

    explicit BlockingJob(Arc<blocking_pool::JobState> state): m_state(rstd::move(state)) {}

public:
    BlockingJob(const BlockingJob&)                        = delete;
    auto operator=(const BlockingJob&) -> BlockingJob&     = delete;
    BlockingJob(BlockingJob&&) noexcept                    = default;
    auto operator=(BlockingJob&&) noexcept -> BlockingJob& = default;

    template<typename Run, typename Cancel>
    static auto make(Run&& run, Cancel&& cancel) -> BlockingJob {
        return BlockingJob { Arc<blocking_pool::JobState>::make(
            blocking_pool::JobFn::make(rstd::forward<Run>(run)),
            blocking_pool::JobFn::make(rstd::forward<Cancel>(cancel))) };
    }

    auto cancellation() const -> BlockingJobCancellation {
        return BlockingJobCancellation { m_state.downgrade() };
    }

    void run() { m_state->run(); }
    auto cancel_queued() -> bool { return m_state->cancel_queued(); }
};

} // namespace rstd::async

namespace rstd::async::blocking_pool
{

struct PoolFields {
    Vec<Option<BlockingJob>>      queue;
    usize                         queue_head {};
    usize                         queued {};
    usize                         current_threads {};
    usize                         idle_threads {};
    usize                         forced_spawn_failures {};
    bool                          accepting { true };
    Vec<thread::JoinHandle<void>> workers;
    time::Duration                keep_alive;

    explicit PoolFields(time::Duration keep_alive)
        : queue(Vec<Option<BlockingJob>>::make()),
          workers(Vec<thread::JoinHandle<void>>::make()),
          keep_alive(keep_alive) {}
};

struct PoolState {
    sync::Mutex<PoolFields> fields;
    sync::Condvar           work_available;
    sync::Condvar           quiescent;
    usize                   max_threads;
    Option<String>          thread_name;

    PoolState(usize max_threads, Option<String> thread_name)
        : fields(PoolFields { time::Duration::from_secs(u64(10)) }),
          work_available(sync::Condvar::make()),
          quiescent(sync::Condvar::make()),
          max_threads(max_threads),
          thread_name(rstd::move(thread_name)) {}
};

auto take_next(PoolFields& fields) -> Option<BlockingJob> {
    auto job = fields.queue[fields.queue_head].take();
    ++fields.queue_head;
    --fields.queued;

    if (fields.queued == usize()) {
        fields.queue      = Vec<Option<BlockingJob>>::make();
        fields.queue_head = usize();
    } else if (fields.queue_head >= usize(64) &&
               fields.queue_head >= fields.queue.len() / usize(2)) {
        auto compacted = Vec<Option<BlockingJob>>::with_capacity(fields.queued);
        for (auto index = fields.queue_head; index < fields.queue.len(); ++index) {
            compacted.push(fields.queue[index].take());
        }
        fields.queue      = rstd::move(compacted);
        fields.queue_head = usize();
    }
    return job;
}

void worker(Arc<PoolState> state) {
    for (;;) {
        auto job = Option<BlockingJob> {};
        {
            auto fields = state->fields.lock().unwrap_unchecked();
            while (fields->queued == usize() && fields->accepting) {
                ++fields->idle_threads;
                auto waited = state->work_available.wait_timeout_while(
                    fields, fields->keep_alive, [](const PoolFields& value) {
                        return value.queued == usize() && value.accepting;
                    });
                --fields->idle_threads;
                if (waited.timed_out() && fields->queued == usize() && fields->accepting) {
                    --fields->current_threads;
                    state->quiescent.notify_all();
                    return;
                }
            }

            if (! fields->accepting && fields->queued == usize()) {
                --fields->current_threads;
                state->quiescent.notify_all();
                return;
            }
            job = take_next(*fields);
        }
        rstd::move(job).unwrap_unchecked().run();
    }
}

auto take_finished_workers(PoolState& state) -> Vec<thread::JoinHandle<void>> {
    auto finished = Vec<thread::JoinHandle<void>>::make();
    auto fields   = state.fields.lock().unwrap_unchecked();
    for (auto index = fields->workers.len(); index > usize();) {
        --index;
        if (fields->workers[index].is_finished()) {
            finished.push(fields->workers.remove(index));
        }
    }
    return finished;
}

void join_all(Vec<thread::JoinHandle<void>> workers) {
    auto owned = rstd::move(workers);
    while (! owned.is_empty()) {
        auto worker = rstd::move(owned.pop()).unwrap_unchecked();
        (void)rstd::move(worker).join();
    }
}

auto submit(const Arc<PoolState>& state, BlockingJob job) -> io::Result<BlockingJobCancellation> {
    join_all(take_finished_workers(*state));

    auto cancellation = job.cancellation();
    bool spawn_worker = false;
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (! fields->accepting) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        fields->queue.push(Some(rstd::move(job)));
        ++fields->queued;
        if (fields->idle_threads != usize()) {
            state->work_available.notify_one();
        } else if (fields->current_threads < state->max_threads) {
            ++fields->current_threads;
            spawn_worker = true;
        }
    }

    if (! spawn_worker) return Ok(rstd::move(cancellation));

    auto builder = thread::builder::Builder::make();
    if (state->thread_name.is_some()) {
        builder.name(state->thread_name->clone());
    }
    auto force_spawn_failure = false;
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->forced_spawn_failures != usize()) {
            --fields->forced_spawn_failures;
            force_spawn_failure = true;
        }
    }
    auto worker = [&]() -> io::Result<thread::JoinHandle<void>> {
        if (force_spawn_failure) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Other }));
        }
        return builder.spawn([state = state.clone()]() mutable {
            blocking_pool::worker(rstd::move(state));
        });
    }();
    if (worker.is_ok()) {
        auto fields = state->fields.lock().unwrap_unchecked();
        fields->workers.push(rstd::move(worker).unwrap_unchecked());
        return Ok(rstd::move(cancellation));
    }

    auto error     = rstd::move(worker).unwrap_err_unchecked();
    auto cancelled = Vec<BlockingJob>::make();
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        --fields->current_threads;
        if (fields->current_threads != usize()) {
            return Ok(rstd::move(cancellation));
        }
        fields->accepting = false;
        cancelled.reserve(fields->queued);
        for (auto index = fields->queue_head; index < fields->queue.len(); ++index) {
            auto queued = fields->queue[index].take();
            if (queued.is_some()) {
                cancelled.push(rstd::move(queued).unwrap_unchecked());
            }
        }
        fields->queue      = Vec<Option<BlockingJob>>::make();
        fields->queue_head = usize();
        fields->queued     = usize();
        state->work_available.notify_all();
        state->quiescent.notify_all();
    }
    while (! cancelled.is_empty()) {
        (void)rstd::move(cancelled.pop()).unwrap_unchecked().cancel_queued();
    }
    return Err(rstd::move(error));
}

} // namespace rstd::async::blocking_pool

namespace rstd::async
{

class BlockingSpawner {
    Weak<blocking_pool::PoolState> m_state;

    explicit BlockingSpawner(Weak<blocking_pool::PoolState> state): m_state(rstd::move(state)) {}

    friend class RuntimeBlockingPool;

public:
    BlockingSpawner()                                              = delete;
    BlockingSpawner(const BlockingSpawner&)                        = delete;
    auto operator=(const BlockingSpawner&) -> BlockingSpawner&     = delete;
    BlockingSpawner(BlockingSpawner&&) noexcept                    = default;
    auto operator=(BlockingSpawner&&) noexcept -> BlockingSpawner& = default;

    auto clone() const -> BlockingSpawner { return BlockingSpawner { m_state.clone() }; }

    auto submit(BlockingJob job) const -> io::Result<BlockingJobCancellation> {
        auto state = m_state.upgrade();
        if (! state) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        return blocking_pool::submit(state, rstd::move(job));
    }
};

class RuntimeBlockingPool {
    Arc<blocking_pool::PoolState> m_state;

public:
    RuntimeBlockingPool(usize max_threads, Option<String> thread_name)
        : m_state(Arc<blocking_pool::PoolState>::make(max_threads, rstd::move(thread_name))) {}

    RuntimeBlockingPool(const RuntimeBlockingPool&)                    = delete;
    auto operator=(const RuntimeBlockingPool&) -> RuntimeBlockingPool& = delete;

    auto spawner() const -> BlockingSpawner { return BlockingSpawner { m_state.downgrade() }; }

    auto thread_count() const -> usize {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        return fields->current_threads;
    }

    void set_keep_alive(time::Duration keep_alive) {
        auto fields        = m_state->fields.lock().unwrap_unchecked();
        fields->keep_alive = keep_alive;
    }

    void fail_next_spawn() {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        ++fields->forced_spawn_failures;
    }

    void begin_shutdown() {
        auto cancelled = Vec<BlockingJob>::make();
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            if (! fields->accepting) return;
            fields->accepting = false;
            cancelled.reserve(fields->queued);
            for (auto index = fields->queue_head; index < fields->queue.len(); ++index) {
                auto job = fields->queue[index].take();
                if (job.is_some()) cancelled.push(rstd::move(job).unwrap_unchecked());
            }
            fields->queue      = Vec<Option<BlockingJob>>::make();
            fields->queue_head = usize();
            fields->queued     = usize();
            m_state->work_available.notify_all();
        }
        while (! cancelled.is_empty()) {
            (void)rstd::move(cancelled.pop()).unwrap_unchecked().cancel_queued();
        }
    }

    void join() {
        begin_shutdown();
        auto workers = Vec<thread::JoinHandle<void>>::make();
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            m_state->quiescent.wait_while(fields, [](const blocking_pool::PoolFields& value) {
                return value.current_threads != usize();
            });
            workers         = rstd::move(fields->workers);
            fields->workers = Vec<thread::JoinHandle<void>>::make();
        }
        blocking_pool::join_all(rstd::move(workers));
    }
};

} // namespace rstd::async
