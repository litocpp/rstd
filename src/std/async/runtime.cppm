export module rstd:async.runtime;
export import :async.awaitable;
export import :async.runtime_core;
import :async.runtime_driver;
import :async.spawn;
import :async.task;
import :io;
import rstd.alloc;
import :sync;
import :thread;

using ::alloc::vec::Vec;
using ::alloc::string::String;

namespace rstd::async
{

export class RuntimeBuilder;

export class RuntimeHandle {
    sync::Weak<RuntimeInner> m_inner;

    explicit RuntimeHandle(sync::Weak<RuntimeInner> inner) : m_inner(rstd::move(inner)) {}

    friend class Runtime;

public:
    RuntimeHandle(const RuntimeHandle&)            = delete;
    auto operator=(const RuntimeHandle&) -> RuntimeHandle& = delete;
    RuntimeHandle(RuntimeHandle&&) noexcept                    = default;
    auto operator=(RuntimeHandle&&) noexcept -> RuntimeHandle& = default;
    ~RuntimeHandle()                                           = default;

    auto clone() const -> RuntimeHandle { return RuntimeHandle { m_inner.clone() }; }

    template<AwaitableInput A>
    auto spawn(A awaitable) const -> JoinHandle<await_output_t<A>> {
        auto inner = m_inner.upgrade();
        if (! inner) {
            rstd::panic { "RuntimeHandle::spawn called after runtime shutdown" };
        }
        auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
        return ::spawn_driver_on(*inner.as_ptr().as_raw_ptr(), rstd::move(driver));
    }
};

export class Runtime {
    sync::Arc<RuntimeInner>       m_inner;
    Vec<thread::JoinHandle<void>> m_workers;

    friend class RuntimeBuilder;

    explicit Runtime(RuntimeKind kind, RuntimeConfig config)
        : m_inner(sync::Arc<RuntimeInner>::make(kind, config)),
          m_workers(Vec<thread::JoinHandle<void>>::make()) {
        m_inner->init_self(m_inner.downgrade());
    }

    static auto make_thread_pool(usize worker_threads,
                                 Option<String> const& thread_name,
                                 RuntimeConfig config)
        -> io::Result<Runtime> {
        auto runtime = Runtime { RuntimeKind::ThreadPool, config };

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
    Runtime() : Runtime(RuntimeKind::CurrentThread, RuntimeConfig::all()) {}

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

    auto handle() const -> RuntimeHandle { return RuntimeHandle { m_inner.downgrade() }; }

    auto io_enabled() const -> bool { return m_inner->io_enabled(); }

    auto time_enabled() const -> bool { return m_inner->time_enabled(); }

    template<AwaitableInput A>
    auto spawn(A awaitable) -> JoinHandle<await_output_t<A>> {
        auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
        return ::spawn_driver_on(*m_inner.as_ptr().as_raw_ptr(), rstd::move(driver));
    }

    template<AwaitableInput A>
    auto spawn_local(A awaitable) -> JoinHandle<await_output_t<A>> {
        if (m_inner->is_thread_pool()) {
            rstd::panic { "spawn_local is only supported by current-thread runtime" };
        }
        auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
        return ::spawn_driver_on(*m_inner.as_ptr().as_raw_ptr(), rstd::move(driver));
    }

    template<AwaitableInput A>
    auto block_on(A awaitable) -> await_output_t<A> {
        auto& runtime = *m_inner.as_ptr().as_raw_ptr();
        auto  scope   = RuntimeScope { runtime };
        auto  parker  = ParkerScope { runtime };
        auto  waker   = make_runtime_waker(runtime);
        auto  cx      = task::Context { waker };
        auto  driver  = make_runtime_driver(into_coro(rstd::move(awaitable)));

        using BlockOnDriver = decltype(driver);

        struct BlockOnExternalState {
            RuntimeInner*                               runtime;
            BlockOnDriver*                              driver;
            task::Waker                                 waker;
            sync::Arc<sync::atomic::Atomic<bool>>       done;

            BlockOnExternalState(RuntimeInner* runtime,
                                 BlockOnDriver* driver,
                                 task::Waker waker,
                                 sync::Arc<sync::atomic::Atomic<bool>> done)
                : runtime(runtime),
                  driver(driver),
                  waker(rstd::move(waker)),
                  done(rstd::move(done)) {}

            static auto post(ResumePlacement& placement,
                             sync::Arc<BlockOnExternalState> state) -> bool {
                return placement.post_external(ResumeJob::make(
                    [state = rstd::move(state)]() mutable {
                        BlockOnExternalState::run(rstd::move(state));
                    }));
            }

            static void run(sync::Arc<BlockOnExternalState> state) {
                auto scope  = RuntimeScope { *state->runtime };
                auto domain = ExecutionDomainScope { ExecutionDomainKind::ExternalExecutor };
                auto cx     = task::Context { state->waker };
                auto out    = state->driver->resume_external_segment(cx);
                if (out.is_external()) {
                    auto placement = out.take_placement();
                    auto posted    = post(placement, state.clone());
                    if (! posted) {
                        placement.reject_external();
                        state->done->store(true, sync::atomic::Ordering::Release);
                        state->waker.clone().wake();
                    }
                    return;
                }

                state->done->store(true, sync::atomic::Ordering::Release);
                state->waker.clone().wake();
            }
        };

        while (true) {
            auto out = driver.drive(cx);
            if (out.is_ready()) {
                m_inner->drain_ready();
                if constexpr (mtp::is_void<await_output_t<A>>) {
                    out.take();
                    return;
                } else {
                    return out.take();
                }
            }
            if (out.is_external()) {
                auto placement = out.take_placement();
                auto done      = sync::Arc<sync::atomic::Atomic<bool>>::make(false);
                auto state     = sync::Arc<BlockOnExternalState>::make(
                    rstd::addressof(runtime),
                    rstd::addressof(driver),
                    cx.waker().clone(),
                    done.clone());
                auto posted = BlockOnExternalState::post(placement, rstd::move(state));
                if (! posted) {
                    placement.reject_external();
                    continue;
                }

                while (! done->load(sync::atomic::Ordering::Acquire)) {
                    m_inner->drain_ready();
                    m_inner->wait_for_work();
                }
                continue;
            }

            m_inner->drain_ready();
            m_inner->wait_for_work();
        }
    }
};

export class RuntimeBuilder {
    RuntimeKind    m_kind;
    usize          m_worker_threads;
    Option<String> m_thread_name;
    RuntimeConfig  m_config;

    RuntimeBuilder(RuntimeKind kind, usize worker_threads)
        : m_kind(kind),
          m_worker_threads(worker_threads),
          m_thread_name(None()),
          m_config(RuntimeConfig {}) {}

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

    auto enable_io() -> RuntimeBuilder& {
        m_config.enable_io = true;
        return *this;
    }

    auto enable_time() -> RuntimeBuilder& {
        m_config.enable_time = true;
        return *this;
    }

    auto enable_all() -> RuntimeBuilder& {
        enable_io();
        enable_time();
        return *this;
    }

    auto build() -> io::Result<Runtime> {
        if (m_kind == RuntimeKind::CurrentThread) {
            return Ok(Runtime { RuntimeKind::CurrentThread, m_config });
        }

        if (m_worker_threads == 0) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }

        return Runtime::make_thread_pool(m_worker_threads, m_thread_name, m_config);
    }
};

export template<AwaitableInput A>
auto block_on(A awaitable) -> await_output_t<A> {
    auto runtime = Runtime {};
    return runtime.block_on(rstd::move(awaitable));
}

} // namespace rstd::async
