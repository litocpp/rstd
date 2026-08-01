export module rstd:thread.blocking_task_set;
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

export enum class BlockingTaskSetSubmitError {
    Closed,
    Cancelled,
    Full,
};

export template<typename T>
class BlockingTaskCompletion {
public:
    usize id() const noexcept { return m_id; }
    bool  is_completed() const noexcept { return m_value.is_some(); }
    bool  is_cancelled() const noexcept { return m_cancelled; }

    auto value() & -> Option<T>& { return m_value; }
    auto value() const& -> const Option<T>& { return m_value; }
    auto into_value() && -> Option<T> { return rstd::move(m_value); }

    static auto completed(usize id, T value) -> BlockingTaskCompletion {
        return BlockingTaskCompletion(id, Some(rstd::move(value)), false);
    }

    static auto cancelled(usize id) -> BlockingTaskCompletion {
        return BlockingTaskCompletion(id, Option<T> {}, true);
    }

private:
    usize     m_id {};
    Option<T> m_value;
    bool      m_cancelled { false };

    BlockingTaskCompletion(usize id, Option<T> value, bool cancelled)
        : m_id(id), m_value(rstd::move(value)), m_cancelled(cancelled) {}
};

namespace blocking_task_set
{

template<typename T>
struct Entry {
    usize                id {};
    Box<dyn<FnMut<T()>>> job;
};

template<typename T>
struct Fields {
    Vec<Option<BlockingTaskCompletion<T>>> completions;
    usize                                  completion_head {};
    usize                                  completion_tail {};
    usize                                  completion_count {};
    usize                                  in_flight {};
    usize                                  active {};
    usize                                  next_id {};
    bool                                   closed { false };
    bool                                   cancelling { false };

    explicit Fields(usize max_in_flight)
        : completions(Vec<Option<BlockingTaskCompletion<T>>>::with_capacity(max_in_flight)) {
        for (rstd::size_t index = 0; index < max_in_flight.to_primitive(); ++index) {
            completions.push(None());
        }
    }
};

template<typename T>
struct Shared {
    sync::Mutex<Fields<T>> fields;
    sync::Condvar          completion_available;
    ThreadPoolHandle       pool;
    usize                  max_in_flight;

    Shared(ThreadPoolHandle pool_, usize max_in_flight_)
        : fields(Fields<T> { max_in_flight_ }),
          completion_available(sync::Condvar::make()),
          pool(rstd::move(pool_)),
          max_in_flight(max_in_flight_) {}
};

template<typename T>
void publish(const Arc<Shared<T>>& shared, BlockingTaskCompletion<T> completion) {
    auto fields                                  = shared->fields.lock().unwrap_unchecked();
    fields->completions[fields->completion_tail] = Some(rstd::move(completion));
    fields->completion_tail = (fields->completion_tail + usize(1)) % shared->max_in_flight;
    ++fields->completion_count;
    --fields->active;
    shared->completion_available.notify_all();
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
            publish(m_shared, BlockingTaskCompletion<T>::cancelled(entry.as_ref().unwrap().id));
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
            publish(m_shared, BlockingTaskCompletion<T>::cancelled(entry.id));
            return;
        }
        auto value = entry.job->operator()();
        publish(m_shared, BlockingTaskCompletion<T>::completed(entry.id, rstd::move(value)));
    }

    void rollback() {
        auto entry = m_entry.take();
        if (entry.is_none()) return;
        auto fields    = m_shared->fields.lock().unwrap_unchecked();
        fields->closed = true;
        --fields->active;
        --fields->in_flight;
        m_shared->completion_available.notify_all();
    }
};

template<typename T>
void wait_inactive(const Arc<Shared<T>>& shared) {
    auto fields = shared->fields.lock().unwrap_unchecked();
    shared->completion_available.wait_while(fields, [](const Fields<T>& fields) {
        return fields.active != usize();
    });
}

} // namespace blocking_task_set

export template<typename T>
class BlockingTaskSet {
    using Shared = blocking_task_set::Shared<T>;

public:
    BlockingTaskSet(const BlockingTaskSet&)                        = delete;
    auto operator=(const BlockingTaskSet&) -> BlockingTaskSet&     = delete;
    BlockingTaskSet(BlockingTaskSet&&) noexcept                    = default;
    auto operator=(BlockingTaskSet&&) noexcept -> BlockingTaskSet& = delete;

    ~BlockingTaskSet() {
        if (! m_shared) return;
        cancel_pending();
        blocking_task_set::wait_inactive(m_shared);
    }

    static auto make(ThreadPoolHandle pool, usize max_in_flight) -> io::Result<BlockingTaskSet> {
        if (max_in_flight == usize() || pool.worker_count() == usize()) {
            return Err(io::Error::new_const(io::ErrorKind { io::ErrorKind::InvalidInput },
                                            "blocking task set requires non-zero capacity"));
        }
        return Ok(BlockingTaskSet(Arc<Shared>::make(rstd::move(pool), max_in_flight)));
    }

    template<typename F>
    auto try_submit(F&& task) -> rstd::result::Result<usize, BlockingTaskSetSubmitError>
        requires mtp::same_as<mtp::invoke_result_t<F>, T>
    {
        usize id {};
        {
            auto fields = m_shared->fields.lock().unwrap_unchecked();
            if (fields->cancelling) return Err(BlockingTaskSetSubmitError::Cancelled);
            if (fields->closed) return Err(BlockingTaskSetSubmitError::Closed);
            if (fields->in_flight >= m_shared->max_in_flight) {
                return Err(BlockingTaskSetSubmitError::Full);
            }
            id = fields->next_id;
            ++fields->next_id;
            ++fields->in_flight;
            ++fields->active;
        }

        auto runner = blocking_task_set::Runner<T>(
            m_shared.clone(),
            blocking_task_set::Entry<T> {
                .id  = id,
                .job = Box<dyn<FnMut<T()>>>::make([task = rstd::forward<F>(task)]() mutable -> T {
                    return task();
                }),
            });
        auto posted = m_shared->pool.post_or_return(rstd::move(runner));
        if (posted.is_err()) {
            auto rejected = rstd::move(posted).unwrap_err_unchecked();
            rejected.rollback();
            return Err(BlockingTaskSetSubmitError::Closed);
        }
        return Ok(id);
    }

    auto recv() -> Option<BlockingTaskCompletion<T>> {
        auto fields = m_shared->fields.lock().unwrap_unchecked();
        m_shared->completion_available.wait_while(fields, [](const auto& fields) {
            return fields.completion_count == usize() &&
                   ! (fields.closed && fields.active == usize());
        });
        if (fields->completion_count == usize()) return None();

        auto completion         = fields->completions[fields->completion_head].take();
        fields->completion_head = (fields->completion_head + usize(1)) % m_shared->max_in_flight;
        --fields->completion_count;
        --fields->in_flight;
        return completion;
    }

    void close() {
        auto fields    = m_shared->fields.lock().unwrap_unchecked();
        fields->closed = true;
        m_shared->completion_available.notify_all();
    }

    void cancel_pending() {
        auto fields        = m_shared->fields.lock().unwrap_unchecked();
        fields->closed     = true;
        fields->cancelling = true;
        m_shared->completion_available.notify_all();
    }

    auto max_in_flight() const noexcept -> usize { return m_shared->max_in_flight; }

private:
    explicit BlockingTaskSet(Arc<Shared> shared): m_shared(rstd::move(shared)) {}

    Arc<Shared> m_shared;
};

} // namespace rstd::thread
