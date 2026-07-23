export module rstd:thread.blocking_task_group;
export import :io;
export import :thread.thread_pool;
export import rstd.alloc;
export import rstd.core;
import :sync.condvar;
import :sync.mutex;

using ::alloc::sync::Arc;
using rstd_alloc::boxed::Box;
using rstd_alloc::vec::Vec;

namespace rstd::thread
{

export enum class BlockingSubmitError {
    Closed,
    Cancelled,
};

export template<typename T>
class BlockingTaskOutcome {
public:
    bool is_completed() const noexcept { return m_value.is_some(); }
    bool is_cancelled() const noexcept { return m_cancelled; }

    auto value() & -> Option<T>& { return m_value; }
    auto value() const& -> const Option<T>& { return m_value; }
    auto into_value() && -> Option<T> { return rstd::move(m_value); }

private:
    template<typename>
    friend class BlockingTaskGroup;

    Option<T> m_value;
    bool      m_cancelled { false };

    BlockingTaskOutcome(Option<T> value, bool cancelled)
        : m_value(rstd::move(value)), m_cancelled(cancelled) {}
};

namespace blocking_task_group
{

template<typename T>
struct Entry {
    usize                index {};
    Box<dyn<FnMut<T()>>> job;
};

template<typename T>
struct Fields {
    Vec<Option<Entry<T>>> queue;
    usize                 queue_head {};
    usize                 queue_tail {};
    usize                 queued {};
    usize                 active {};
    Vec<Option<T>>        results;
    Vec<bool>             cancelled;
    Vec<bool>             terminal;
    usize                 terminal_count {};
    bool                  closed { false };
    bool                  cancelling { false };

    explicit Fields(usize queue_capacity)
        : queue(Vec<Option<Entry<T>>>::with_capacity(queue_capacity)),
          results(Vec<Option<T>>::make()),
          cancelled(Vec<bool>::make()),
          terminal(Vec<bool>::make()) {
        for (rstd::size_t index = 0; index < queue_capacity.to_primitive(); ++index) {
            queue.push(None());
        }
    }
};

template<typename T>
struct Shared {
    sync::Mutex<Fields<T>> fields;
    sync::Condvar          space_available;
    sync::Condvar          terminal_available;
    ThreadPoolHandle       pool;
    usize                  parallelism;
    usize                  queue_capacity;
    usize                  worker_count;

    Shared(ThreadPoolHandle pool_, usize parallelism_, usize queue_capacity_)
        : fields(Fields<T> { queue_capacity_ }),
          space_available(sync::Condvar::make()),
          terminal_available(sync::Condvar::make()),
          pool(rstd::move(pool_)),
          parallelism(parallelism_),
          queue_capacity(queue_capacity_),
          worker_count(pool.worker_count()) {}
};

template<typename T>
void dispatch(const Arc<Shared<T>>& shared);

template<typename T>
void finish_active(const Arc<Shared<T>>& shared, usize index, Option<T> value, bool cancelled) {
    {
        auto fields = shared->fields.lock().unwrap_unchecked();
        if (fields->terminal[index]) return;
        fields->results[index]   = rstd::move(value);
        fields->cancelled[index] = cancelled;
        fields->terminal[index]  = true;
        --fields->active;
        ++fields->terminal_count;
        shared->terminal_available.notify_all();
    }
    dispatch(shared);
}

template<typename T>
class Runner {
    Arc<Shared<T>>   m_shared;
    Option<Entry<T>> m_entry;

public:
    Runner(Arc<Shared<T>> shared, Entry<T> entry)
        : m_shared(rstd::move(shared)), m_entry(Some(rstd::move(entry))) {}

    Runner(const Runner&)                    = delete;
    auto operator=(const Runner&) -> Runner& = delete;
    Runner(Runner&& other) noexcept
        : m_shared(rstd::move(other.m_shared)), m_entry(other.m_entry.take()) {}
    auto operator=(Runner&&) noexcept -> Runner& = delete;

    ~Runner() {
        auto entry = m_entry.take();
        if (entry.is_some()) {
            finish_active(m_shared, entry.as_ref().unwrap().index, Option<T> {}, true);
        }
    }

    void operator()() {
        auto entry  = m_entry.take().unwrap_unchecked();
        bool cancel = false;
        {
            auto fields = m_shared->fields.lock().unwrap_unchecked();
            cancel      = fields->cancelling;
        }
        if (cancel) {
            finish_active(m_shared, entry.index, Option<T> {}, true);
            return;
        }
        auto value = entry.job->operator()();
        finish_active(m_shared, entry.index, Some(rstd::move(value)), false);
    }
};

template<typename T>
auto cancel_queued(const Arc<Shared<T>>& shared, bool cancelling) -> usize {
    auto dropped = Vec<Entry<T>>::make();
    auto count   = usize();
    {
        auto fields        = shared->fields.lock().unwrap_unchecked();
        fields->closed     = true;
        fields->cancelling = fields->cancelling || cancelling;
        count              = fields->queued;
        dropped.reserve(count);
        auto queue_index = fields->queue_head;
        for (rstd::size_t index = 0; index < fields->queued.to_primitive(); ++index) {
            auto entry = fields->queue[queue_index].take();
            if (entry.is_some()) {
                auto task                     = rstd::move(entry).unwrap_unchecked();
                fields->cancelled[task.index] = true;
                fields->terminal[task.index]  = true;
                ++fields->terminal_count;
                dropped.push(rstd::move(task));
            }
            queue_index = (queue_index + usize(1)) % shared->queue_capacity;
        }
        fields->queue_head = usize();
        fields->queue_tail = usize();
        fields->queued     = usize();
        shared->space_available.notify_all();
        shared->terminal_available.notify_all();
    }
    return count;
}

template<typename T>
void dispatch(const Arc<Shared<T>>& shared) {
    while (true) {
        Option<Entry<T>> entry;
        {
            auto fields = shared->fields.lock().unwrap_unchecked();
            if (fields->cancelling || fields->active >= shared->parallelism ||
                fields->queued == usize()) {
                return;
            }

            entry              = fields->queue[fields->queue_head].take();
            fields->queue_head = (fields->queue_head + usize(1)) % shared->queue_capacity;
            --fields->queued;
            ++fields->active;
            shared->space_available.notify_one();
        }

        auto runner = Runner<T>(shared.clone(), rstd::move(entry).unwrap_unchecked());
        auto posted = shared->pool.post_or_return(rstd::move(runner));
        if (posted.is_err()) {
            (void)cancel_queued(shared, false);
            (void)rstd::move(posted).unwrap_err_unchecked();
            return;
        }
    }
}

template<typename T>
void wait_terminal(const Arc<Shared<T>>& shared) {
    auto fields = shared->fields.lock().unwrap_unchecked();
    shared->terminal_available.wait_while(fields, [](const Fields<T>& fields) {
        return fields.terminal_count < fields.results.len();
    });
}

} // namespace blocking_task_group

export template<typename T>
class BlockingTaskGroupCancelHandle {
    using Shared = blocking_task_group::Shared<T>;

public:
    BlockingTaskGroupCancelHandle(const BlockingTaskGroupCancelHandle&)                    = delete;
    auto operator=(const BlockingTaskGroupCancelHandle&) -> BlockingTaskGroupCancelHandle& = delete;
    BlockingTaskGroupCancelHandle(BlockingTaskGroupCancelHandle&&) noexcept = default;
    auto operator=(BlockingTaskGroupCancelHandle&&) noexcept
        -> BlockingTaskGroupCancelHandle& = default;

    auto clone() const -> BlockingTaskGroupCancelHandle {
        return BlockingTaskGroupCancelHandle(m_shared.clone());
    }

    auto cancel_pending() const -> usize {
        return blocking_task_group::cancel_queued(m_shared, true);
    }

    bool is_cancelled() const {
        auto fields = m_shared->fields.lock().unwrap_unchecked();
        return fields->cancelling;
    }

private:
    template<typename>
    friend class BlockingTaskGroup;

    explicit BlockingTaskGroupCancelHandle(Arc<Shared> shared): m_shared(rstd::move(shared)) {}

    Arc<Shared> m_shared;
};

export template<typename T>
class BlockingTaskGroup {
    using Shared = blocking_task_group::Shared<T>;

public:
    BlockingTaskGroup(const BlockingTaskGroup&)                = delete;
    BlockingTaskGroup& operator=(const BlockingTaskGroup&)     = delete;
    BlockingTaskGroup(BlockingTaskGroup&&) noexcept            = default;
    BlockingTaskGroup& operator=(BlockingTaskGroup&&) noexcept = delete;

    ~BlockingTaskGroup() {
        if (! m_shared) return;
        cancel_pending();
        cancel_private_pool();
        blocking_task_group::wait_terminal(m_shared);
        join_private_pool();
    }

    static auto make(usize worker_count, usize queue_capacity) -> io::Result<BlockingTaskGroup> {
        if (worker_count == usize() || queue_capacity == usize()) {
            return Err(io::Error::new_const(io::ErrorKind { io::ErrorKind::InvalidInput },
                                            "blocking task group requires non-zero capacity"));
        }
        auto pool = ThreadPoolBuilder::make().worker_count(worker_count).build();
        if (pool.is_err()) return Err(rstd::move(pool).unwrap_err_unchecked());

        auto owned  = rstd::move(pool).unwrap_unchecked();
        auto handle = owned.handle();
        return Ok(
            BlockingTaskGroup(Arc<Shared>::make(rstd::move(handle), worker_count, queue_capacity),
                              Some(rstd::move(owned))));
    }

    static auto on(ThreadPoolHandle pool, usize parallelism, usize queue_capacity)
        -> io::Result<BlockingTaskGroup> {
        if (parallelism == usize() || queue_capacity == usize() || pool.worker_count() == usize()) {
            return Err(io::Error::new_const(io::ErrorKind { io::ErrorKind::InvalidInput },
                                            "blocking task group requires non-zero capacity"));
        }
        return Ok(BlockingTaskGroup(
            Arc<Shared>::make(rstd::move(pool), parallelism, queue_capacity), None()));
    }

    template<typename F>
    auto submit(F&& task) -> rstd::result::Result<usize, BlockingSubmitError>
        requires mtp::same_as<mtp::invoke_result_t<F>, T>
    {
        usize index {};
        {
            auto fields = m_shared->fields.lock().unwrap_unchecked();
            m_shared->space_available.wait_while(fields, [this](const auto& fields) {
                return fields.queued >= m_shared->queue_capacity && ! fields.closed &&
                       ! fields.cancelling;
            });

            if (fields->cancelling) return Err(BlockingSubmitError::Cancelled);
            if (fields->closed) return Err(BlockingSubmitError::Closed);

            index = fields->results.len();
            fields->results.push(None());
            fields->cancelled.push(false);
            fields->terminal.push(false);
            fields->queue[fields->queue_tail] = Some(blocking_task_group::Entry<T> {
                .index = index,
                .job   = Box<dyn<FnMut<T()>>>::make([task = rstd::forward<F>(task)]() mutable -> T {
                    return task();
                }),
            });
            fields->queue_tail = (fields->queue_tail + usize(1)) % m_shared->queue_capacity;
            ++fields->queued;
        }
        blocking_task_group::dispatch(m_shared);
        return Ok(index);
    }

    void close() {
        {
            auto fields    = m_shared->fields.lock().unwrap_unchecked();
            fields->closed = true;
            m_shared->space_available.notify_all();
        }
        blocking_task_group::dispatch(m_shared);
    }

    auto cancel_pending() -> usize { return blocking_task_group::cancel_queued(m_shared, true); }

    auto join() && -> Vec<BlockingTaskOutcome<T>> {
        close();
        blocking_task_group::wait_terminal(m_shared);
        join_private_pool();

        auto fields   = m_shared->fields.lock().unwrap_unchecked();
        auto outcomes = Vec<BlockingTaskOutcome<T>>::with_capacity(fields->results.len());
        for (rstd::size_t index = 0; index < fields->results.len().to_primitive(); ++index) {
            auto wrapped_index = usize(index);
            outcomes.push(BlockingTaskOutcome<T>(fields->results[wrapped_index].take(),
                                                 fields->cancelled[wrapped_index]));
        }
        return outcomes;
    }

    usize worker_count() const noexcept { return m_shared->worker_count; }
    usize queue_capacity() const noexcept { return m_shared->queue_capacity; }
    auto  cancel_handle() const -> BlockingTaskGroupCancelHandle<T> {
        return BlockingTaskGroupCancelHandle<T>(m_shared.clone());
    }

private:
    BlockingTaskGroup(Arc<Shared> shared, Option<ThreadPool> private_pool)
        : m_shared(rstd::move(shared)), m_private_pool(rstd::move(private_pool)) {}

    void cancel_private_pool() {
        auto pool = m_private_pool.take();
        if (pool.is_none()) return;
        auto owned = rstd::move(pool).unwrap_unchecked();
        owned.cancel_pending();
        m_private_pool = Some(rstd::move(owned));
    }

    void join_private_pool() {
        auto pool = m_private_pool.take();
        if (pool.is_some()) rstd::move(pool).unwrap_unchecked().join();
    }

    Arc<Shared>        m_shared;
    Option<ThreadPool> m_private_pool;
};

} // namespace rstd::thread
