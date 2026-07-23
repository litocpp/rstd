export module rstd:async.thread_pool_executor;
export import :async.executor;
export import :thread.thread_pool;

namespace rstd::async
{

export class ThreadPoolExecutor {
    thread::ThreadPoolHandle m_pool;

    explicit ThreadPoolExecutor(thread::ThreadPoolHandle pool): m_pool(rstd::move(pool)) {}

public:
    ThreadPoolExecutor(const ThreadPoolExecutor&)                        = delete;
    auto operator=(const ThreadPoolExecutor&) -> ThreadPoolExecutor&     = delete;
    ThreadPoolExecutor(ThreadPoolExecutor&&) noexcept                    = default;
    auto operator=(ThreadPoolExecutor&&) noexcept -> ThreadPoolExecutor& = default;

    static auto from_handle(thread::ThreadPoolHandle pool) -> ThreadPoolExecutor {
        return ThreadPoolExecutor { rstd::move(pool) };
    }

    auto clone() const -> ThreadPoolExecutor { return ThreadPoolExecutor { m_pool.clone() }; }

    auto post_job(ExecutorJob job) -> bool {
        return m_pool
            .post([job = rstd::move(job)]() mutable {
                job.run();
            })
            .is_ok();
    }

    template<typename F>
    auto post(F job) -> bool {
        return post_job(ExecutorJob::make(rstd::move(job)));
    }

    auto is_closed() -> bool { return m_pool.is_closed(); }
};

} // namespace rstd::async

namespace rstd
{

template<>
struct Impl<async::Executor, async::ThreadPoolExecutor>
    : LinkClassMethod<async::Executor, async::ThreadPoolExecutor> {};

} // namespace rstd
