module;
#include <rstd/macro.hpp>

export module rstd:thread.blocking_task_group;
export import :io;
export import :thread.functions;
export import rstd.alloc;
export import rstd.core;
import :sync.condvar;
import :sync.mutex;

using ::alloc::sync::Arc;

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
    usize                index { 0 };
    Box<dyn<FnMut<T()>>> job;
};

template<typename T>
struct Fields {
    Vec<Option<Entry<T>>> queue;
    usize                 queue_head { 0 };
    usize                 queue_tail { 0 };
    usize                 queued { 0 };
    usize                 running { 0 };
    Vec<Option<T>>        results;
    Vec<bool>             cancelled;
    bool                  closed { false };
    bool                  cancelling { false };

    explicit Fields(usize queue_capacity)
        : queue(Vec<Option<Entry<T>>>::with_capacity(queue_capacity)) {
        for (usize index = 0; index < queue_capacity; ++index) queue.push(None());
    }
};

template<typename T>
struct Shared {
    sync::Mutex<Fields<T>> fields;
    sync::Condvar          work_available;
    sync::Condvar          space_available;
    usize                  queue_capacity { 0 };

    explicit Shared(usize capacity)
        : fields(Fields<T> { capacity }),
          work_available(sync::Condvar::make()),
          space_available(sync::Condvar::make()),
          queue_capacity(capacity) {}
};

template<typename T>
void worker(Arc<Shared<T>> shared) {
    while (true) {
        Option<Entry<T>> entry;
        {
            auto fields = shared->fields.lock().unwrap_unchecked();
            shared->work_available.wait_while(fields, [](const Fields<T>& fields) {
                return fields.queued == 0 && ! fields.closed && ! fields.cancelling;
            });

            if (fields->cancelling || (fields->closed && fields->queued == 0)) return;

            entry              = fields->queue[fields->queue_head].take();
            fields->queue_head = (fields->queue_head + 1) % shared->queue_capacity;
            --fields->queued;
            ++fields->running;
        }
        shared->space_available.notify_one();

        auto task  = rstd::move(entry).unwrap_unchecked();
        auto value = task.job->operator()();

        {
            auto fields                 = shared->fields.lock().unwrap_unchecked();
            fields->results[task.index] = Some(rstd::move(value));
            --fields->running;
        }
    }
}

template<typename T>
auto cancel_pending(const Arc<Shared<T>>& shared) -> usize {
    auto fields          = shared->fields.lock().unwrap_unchecked();
    fields->closed       = true;
    fields->cancelling   = true;
    auto cancelled_count = fields->queued;
    auto queue_index     = fields->queue_head;
    for (usize index = 0; index < fields->queued; ++index) {
        auto entry = fields->queue[queue_index].take();
        if (entry.is_some()) {
            fields->cancelled[entry->index] = true;
        }
        queue_index = (queue_index + 1) % shared->queue_capacity;
    }
    fields->queue_head = 0;
    fields->queue_tail = 0;
    fields->queued     = 0;
    shared->work_available.notify_all();
    shared->space_available.notify_all();
    return cancelled_count;
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

    auto cancel_pending() const -> usize { return blocking_task_group::cancel_pending(m_shared); }

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
        if (m_workers.is_empty()) return;
        cancel_pending();
        join_workers();
    }

    static auto make(usize worker_count, usize queue_capacity) -> io::Result<BlockingTaskGroup> {
        if (worker_count == 0 || queue_capacity == 0) {
            return Err(io::Error::new_const(io::ErrorKind { io::ErrorKind::InvalidInput },
                                            "blocking task group requires non-zero capacity"));
        }

        auto group = BlockingTaskGroup(Arc<Shared>::make(queue_capacity), worker_count);
        for (usize index = 0; index < worker_count; ++index) {
            auto worker_state = group.m_shared.clone();
            auto spawned      = thread::spawn([state = rstd::move(worker_state)]() mutable {
                blocking_task_group::worker(rstd::move(state));
            });
            if (spawned.is_err()) {
                auto error = rstd::move(spawned).unwrap_err_unchecked();
                group.cancel_pending();
                group.join_workers();
                return Err(rstd::move(error));
            }
            group.m_workers.push(rstd::move(spawned).unwrap_unchecked());
        }
        return Ok(rstd::move(group));
    }

    template<typename F>
    auto submit(F&& task) -> rstd::result::Result<usize, BlockingSubmitError>
        requires mtp::same_as<mtp::invoke_result_t<F>, T>
    {
        auto fields = m_shared->fields.lock().unwrap_unchecked();
        m_shared->space_available.wait_while(fields, [this](const auto& fields) {
            return fields.queued >= m_shared->queue_capacity && ! fields.closed &&
                   ! fields.cancelling;
        });

        if (fields->cancelling) return Err(BlockingSubmitError::Cancelled);
        if (fields->closed) return Err(BlockingSubmitError::Closed);

        const auto index = fields->results.len();
        fields->results.push(None());
        fields->cancelled.push(false);
        fields->queue[fields->queue_tail] = Some(blocking_task_group::Entry<T> {
            .index = index,
            .job   = Box<dyn<FnMut<T()>>>::make([task = rstd::forward<F>(task)]() mutable -> T {
                return task();
            }),
        });
        fields->queue_tail                = (fields->queue_tail + 1) % m_shared->queue_capacity;
        ++fields->queued;
        m_shared->work_available.notify_one();
        return Ok(index);
    }

    void close() {
        auto fields    = m_shared->fields.lock().unwrap_unchecked();
        fields->closed = true;
        m_shared->work_available.notify_all();
        m_shared->space_available.notify_all();
    }

    auto cancel_pending() -> usize { return blocking_task_group::cancel_pending(m_shared); }

    auto join() && -> Vec<BlockingTaskOutcome<T>> {
        close();
        join_workers();

        auto fields   = m_shared->fields.lock().unwrap_unchecked();
        auto outcomes = Vec<BlockingTaskOutcome<T>>::with_capacity(fields->results.len());
        for (usize index = 0; index < fields->results.len(); ++index) {
            outcomes.push(
                BlockingTaskOutcome<T>(fields->results[index].take(), fields->cancelled[index]));
        }
        return outcomes;
    }

    usize worker_count() const noexcept { return m_worker_count; }
    usize queue_capacity() const noexcept { return m_shared->queue_capacity; }
    auto  cancel_handle() const -> BlockingTaskGroupCancelHandle<T> {
        return BlockingTaskGroupCancelHandle<T>(m_shared.clone());
    }

private:
    BlockingTaskGroup(Arc<Shared> shared, usize worker_count)
        : m_shared(rstd::move(shared)), m_worker_count(worker_count) {}

    void join_workers() {
        while (! m_workers.is_empty()) {
            auto worker = m_workers.pop().unwrap_unchecked();
            (void)rstd::move(worker).join();
        }
    }

    Arc<Shared>           m_shared;
    Vec<JoinHandle<void>> m_workers;
    usize                 m_worker_count { 0 };
};

} // namespace rstd::thread
