export module rstd:thread.thread_pool;
export import :io;
export import :thread.builder;
export import rstd.alloc;
export import rstd.core;
import :sync.condvar;
import :sync.mutex;

using ::alloc::sync::Arc;
using ::alloc::sync::Weak;
using rstd_alloc::boxed::Box;
using rstd_alloc::string::String;
using rstd_alloc::vec::Vec;

namespace rstd::thread
{

export enum class ThreadPoolPostError {
    Closed,
};

namespace thread_pool
{

using Job = Box<dyn<FnMut<void()>>>;

struct Fields {
    Vec<Option<Job>> queue;
    usize            queue_head {};
    usize            queued {};
    bool             closed { false };
    bool             cancelling { false };

    Fields(): queue(Vec<Option<Job>>::make()) {}
};

struct State {
    sync::Mutex<Fields> fields;
    sync::Condvar       work_available;
    usize               worker_count;

    explicit State(usize count)
        : fields(Fields {}), work_available(sync::Condvar::make()), worker_count(count) {}
};

void worker(Arc<State> state) {
    while (true) {
        Option<Job> job;
        {
            auto fields = state->fields.lock().unwrap_unchecked();
            state->work_available.wait_while(fields, [](const Fields& fields) {
                return fields.queued == usize() && ! fields.closed && ! fields.cancelling;
            });

            if (fields->cancelling || (fields->closed && fields->queued == usize())) return;

            job = fields->queue[fields->queue_head].take();
            ++fields->queue_head;
            --fields->queued;

            if (fields->queued == usize()) {
                fields->queue      = Vec<Option<Job>>::make();
                fields->queue_head = usize();
            } else if (fields->queue_head >= usize(64) &&
                       fields->queue_head >= fields->queue.len() / usize(2)) {
                auto compacted = Vec<Option<Job>>::with_capacity(fields->queued);
                for (auto index = fields->queue_head; index < fields->queue.len(); ++index) {
                    compacted.push(fields->queue[index].take());
                }
                fields->queue      = rstd::move(compacted);
                fields->queue_head = usize();
            }
        }

        rstd::move(job).unwrap_unchecked()->operator()();
    }
}

auto cancel_pending(const Arc<State>& state) -> usize {
    auto cancelled = Vec<Job>::make();
    auto count     = usize();
    {
        auto fields        = state->fields.lock().unwrap_unchecked();
        fields->closed     = true;
        fields->cancelling = true;
        count              = fields->queued;
        cancelled.reserve(count);
        for (auto index = fields->queue_head; index < fields->queue.len(); ++index) {
            auto job = fields->queue[index].take();
            if (job.is_some()) cancelled.push(rstd::move(job).unwrap_unchecked());
        }
        fields->queue      = Vec<Option<Job>>::make();
        fields->queue_head = usize();
        fields->queued     = usize();
        state->work_available.notify_all();
    }
    return count;
}

} // namespace thread_pool

export class ThreadPoolHandle {
    Weak<thread_pool::State> m_state;

    explicit ThreadPoolHandle(Weak<thread_pool::State> state): m_state(rstd::move(state)) {}

    friend class ThreadPool;

public:
    ThreadPoolHandle(const ThreadPoolHandle&)                        = delete;
    auto operator=(const ThreadPoolHandle&) -> ThreadPoolHandle&     = delete;
    ThreadPoolHandle(ThreadPoolHandle&&) noexcept                    = default;
    auto operator=(ThreadPoolHandle&&) noexcept -> ThreadPoolHandle& = default;

    auto clone() const -> ThreadPoolHandle { return ThreadPoolHandle { m_state.clone() }; }

    template<typename F>
    auto post_or_return(F&& task) -> rstd::result::Result<empty, mtp::rm_cvf<F>>
        requires mtp::same_as<mtp::invoke_result_t<F>, void>
    {
        auto owned = mtp::rm_cvf<F>(rstd::forward<F>(task));
        auto state = m_state.upgrade();
        if (! state) return Err(rstd::move(owned));

        {
            auto fields = state->fields.lock().unwrap_unchecked();
            if (fields->closed || fields->cancelling) {
                return Err(rstd::move(owned));
            }
            fields->queue.push(Some(thread_pool::Job::make(rstd::move(owned))));
            ++fields->queued;
            state->work_available.notify_one();
        }
        return Ok(empty {});
    }

    template<typename F>
    auto post(F&& task) -> rstd::result::Result<empty, ThreadPoolPostError>
        requires mtp::same_as<mtp::invoke_result_t<F>, void>
    {
        auto posted = post_or_return(rstd::forward<F>(task));
        if (posted.is_err()) {
            (void)rstd::move(posted).unwrap_err_unchecked();
            return Err(ThreadPoolPostError::Closed);
        }
        return Ok(empty {});
    }

    auto is_closed() const -> bool {
        auto state = m_state.upgrade();
        if (! state) return true;
        auto fields = state->fields.lock().unwrap_unchecked();
        return fields->closed || fields->cancelling;
    }

    auto worker_count() const -> usize {
        auto state = m_state.upgrade();
        return state ? state->worker_count : usize();
    }
};

export class ThreadPool {
    Arc<thread_pool::State> m_state;
    Vec<JoinHandle<void>>   m_workers;

    explicit ThreadPool(Arc<thread_pool::State> state)
        : m_state(rstd::move(state)), m_workers(Vec<JoinHandle<void>>::make()) {}

    friend class ThreadPoolBuilder;

    bool is_current_worker() const {
        const auto current_id = current().id();
        for (const auto& worker : m_workers) {
            if (worker.thread().id() == current_id) return true;
        }
        return false;
    }

    void join_workers() {
        if (is_current_worker()) {
            rstd::panic { "ThreadPool cannot be joined from one of its workers" };
        }
        while (! m_workers.is_empty()) {
            auto worker = m_workers.pop().unwrap_unchecked();
            (void)rstd::move(worker).join();
        }
    }

public:
    ThreadPool(const ThreadPool&)                        = delete;
    auto operator=(const ThreadPool&) -> ThreadPool&     = delete;
    ThreadPool(ThreadPool&&) noexcept                    = default;
    auto operator=(ThreadPool&&) noexcept -> ThreadPool& = delete;

    ~ThreadPool() {
        if (m_workers.is_empty()) return;
        cancel_pending();
        join_workers();
    }

    auto handle() const -> ThreadPoolHandle { return ThreadPoolHandle { m_state.downgrade() }; }

    void close() {
        auto fields    = m_state->fields.lock().unwrap_unchecked();
        fields->closed = true;
        m_state->work_available.notify_all();
    }

    auto cancel_pending() -> usize { return thread_pool::cancel_pending(m_state); }

    void join() && {
        close();
        join_workers();
    }

    auto worker_count() const noexcept -> usize { return m_state->worker_count; }
};

export class ThreadPoolBuilder {
    usize          m_worker_count { usize(1) };
    Option<String> m_thread_name;
    Option<usize>  m_stack_size;

public:
    static auto make() -> ThreadPoolBuilder { return ThreadPoolBuilder {}; }

    auto worker_count(usize count) -> ThreadPoolBuilder& {
        m_worker_count = count;
        return *this;
    }

    auto thread_name(String name) -> ThreadPoolBuilder& {
        m_thread_name = Some(rstd::move(name));
        return *this;
    }

    auto stack_size(usize size) -> ThreadPoolBuilder& {
        m_stack_size = Some(size);
        return *this;
    }

    auto build() -> io::Result<ThreadPool> {
        if (m_worker_count == usize()) {
            return Err(io::Error::new_const(io::ErrorKind { io::ErrorKind::InvalidInput },
                                            "thread pool requires at least one worker"));
        }

        auto pool = ThreadPool(Arc<thread_pool::State>::make(m_worker_count));
        for (rstd::size_t index = 0; index < m_worker_count.to_primitive(); ++index) {
            auto state   = pool.m_state.clone();
            auto builder = thread::builder::Builder::make();
            if (m_thread_name.is_some()) {
                builder.name(rstd::as<rstd::clone::Clone>(*m_thread_name).clone());
            }
            if (m_stack_size.is_some()) builder.stack_size(*m_stack_size);

            auto spawned = builder.spawn([state = rstd::move(state)]() mutable {
                thread_pool::worker(rstd::move(state));
            });
            if (spawned.is_err()) {
                auto error = rstd::move(spawned).unwrap_err_unchecked();
                pool.cancel_pending();
                pool.join_workers();
                return Err(rstd::move(error));
            }
            pool.m_workers.push(rstd::move(spawned).unwrap_unchecked());
        }
        return Ok(rstd::move(pool));
    }
};

} // namespace rstd::thread
