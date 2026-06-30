export module rstd:async.runtime;
export import :async.forward;
export import :async.runtime_core;
import :async.spawn;
import :io;
import rstd.alloc;
import :sync;
import :thread;

using ::alloc::vec::Vec;
using ::alloc::string::String;

namespace rstd::async
{

export class RuntimeBuilder;

export class Runtime {
    sync::Arc<RuntimeInner>       m_inner;
    Vec<thread::JoinHandle<void>> m_workers;

    friend class RuntimeBuilder;

    explicit Runtime(RuntimeKind kind)
        : m_inner(sync::Arc<RuntimeInner>::make(kind)),
          m_workers(Vec<thread::JoinHandle<void>>::make()) {
        m_inner->init_self(m_inner.downgrade());
    }

    static auto make_thread_pool(usize worker_threads, Option<String> const& thread_name)
        -> io::Result<Runtime> {
        auto runtime = Runtime { RuntimeKind::ThreadPool };

        for (usize i = 0; i < worker_threads; ++i) {
            auto inner   = runtime.m_inner.clone();
            auto builder = thread::builder::Builder::make();
            if (thread_name.is_some()) {
                builder.name(rstd::as<rstd::clone::Clone>(*thread_name).clone());
            }

            auto worker = builder.spawn([inner = rstd::move(inner)] {
                inner->worker_loop();
            });

            if (worker.is_err()) {
                auto error = rstd::move(worker).unwrap_err_unchecked();
                runtime.shutdown();
                return Err(rstd::move(error));
            }

            runtime.m_workers.push(rstd::move(worker).unwrap_unchecked());
        }

        return Ok(rstd::move(runtime));
    }

    void shutdown() {
        if (! m_inner || ! m_inner->is_thread_pool()) {
            return;
        }

        m_inner->request_stop();
        while (! m_workers.is_empty()) {
            auto worker = rstd::move(m_workers.pop()).unwrap_unchecked();
            (void)rstd::move(worker).join();
        }
        m_inner->abort_all_tasks();
    }

public:
    Runtime() : Runtime(RuntimeKind::CurrentThread) {}

    Runtime(const Runtime&)            = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) noexcept        = default;
    auto operator=(Runtime&& other) noexcept -> Runtime& {
        if (this != &other) {
            shutdown();
            m_inner   = rstd::move(other.m_inner);
            m_workers = rstd::move(other.m_workers);
        }
        return *this;
    }
    ~Runtime() { shutdown(); }

    template<future::FutureLike F>
    auto spawn(F future) -> JoinHandle<future::future_output_t<F>> {
        return ::spawn_on(*m_inner.as_ptr().as_raw_ptr(), rstd::move(future));
    }

    template<future::FutureLike F>
    auto spawn_local(F future) -> JoinHandle<future::future_output_t<F>> {
        if (m_inner->is_thread_pool()) {
            rstd::panic { "spawn_local is only supported by current-thread runtime" };
        }
        return ::spawn_on(*m_inner.as_ptr().as_raw_ptr(), rstd::move(future));
    }

    template<future::FutureLike F>
    auto block_on(F future) -> future::future_output_t<F> {
        auto& runtime = *m_inner.as_ptr().as_raw_ptr();
        auto  scope   = RuntimeScope { runtime };
        auto  parker  = ParkerScope { runtime };
        auto  waker   = make_runtime_waker(runtime);
        auto  cx      = task::Context { waker };

        while (true) {
            auto out = future::poll(future, cx);
            if (out.is_ready()) {
                m_inner->drain_ready();
                if constexpr (mtp::is_void<future::future_output_t<F>>) {
                    rstd::move(out).take();
                    return;
                } else {
                    return rstd::move(out).take();
                }
            }

            m_inner->drain_ready();
            m_inner->wait_for_work();
        }
    }
};

export class RuntimeBuilder {
    RuntimeKind m_kind;
    usize       m_worker_threads;
    Option<String> m_thread_name;

    RuntimeBuilder(RuntimeKind kind, usize worker_threads)
        : m_kind(kind), m_worker_threads(worker_threads), m_thread_name(None()) {}

public:
    static auto current_thread() -> RuntimeBuilder {
        return RuntimeBuilder { RuntimeKind::CurrentThread, 1 };
    }

    static auto multi_thread() -> RuntimeBuilder {
        return RuntimeBuilder { RuntimeKind::ThreadPool, 1 };
    }

    auto worker_threads(usize n) -> RuntimeBuilder& {
        m_worker_threads = n;
        return *this;
    }

    auto thread_name(String name) -> RuntimeBuilder& {
        m_thread_name = Some(rstd::move(name));
        return *this;
    }

    auto build() -> io::Result<Runtime> {
        if (m_kind == RuntimeKind::CurrentThread) {
            return Ok(Runtime {});
        }

        if (m_worker_threads == 0) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }

        return Runtime::make_thread_pool(m_worker_threads, m_thread_name);
    }
};

export template<future::FutureLike F>
auto block_on(F future) -> future::future_output_t<F> {
    auto runtime = Runtime {};
    return runtime.block_on(rstd::move(future));
}

} // namespace rstd::async
