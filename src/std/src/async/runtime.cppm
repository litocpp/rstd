export module rstd:async.runtime;
export import :async.awaitable;
import :async.runtime_core;
import :async.frame_allocator;
import :async.runtime_driver;
import :async.spawn;
import :async.task;
import rstd.io;
import rstd.alloc;
import :sync;
import :thread;
import :time;

using ::alloc::vec::Vec;
using ::alloc::string::String;
using ::alloc::boxed::Box;
using ::alloc::sync::SharedAllocator;
#if LITO_FEAT_ASYNC_DEBUG

using namespace rstd::literals;
#endif

namespace rstd::async
{

export class RuntimeBuilder;
struct RuntimeBuilderConfigAccess;

export class RuntimeHandle {
    sync::Weak<RuntimeInner> m_inner;

    explicit RuntimeHandle(sync::Weak<RuntimeInner> inner): m_inner(rstd::move(inner)) {}

    friend class Runtime;

public:
    RuntimeHandle(const RuntimeHandle&)                        = delete;
    auto operator=(const RuntimeHandle&) -> RuntimeHandle&     = delete;
    RuntimeHandle(RuntimeHandle&&) noexcept                    = default;
    auto operator=(RuntimeHandle&&) noexcept -> RuntimeHandle& = default;
    ~RuntimeHandle()                                           = default;

    auto clone() const -> RuntimeHandle { return RuntimeHandle { m_inner.clone() }; }

    template<AwaitableInput A>
    auto spawn(A awaitable
#if LITO_FEAT_ASYNC_DEBUG
               ,
               debug::TaskInfo info     = {},
               source_location location = source_location::current()
#endif
    ) const -> JoinHandle<await_output_t<A>> {
        auto inner = m_inner.upgrade();
        if (! inner) {
            rstd::panic { "RuntimeHandle::spawn called after runtime shutdown" };
        }
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        auto scope  = FrameAllocatorScope { inner->frame_allocator() };
        auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
        return ::spawn_driver_on(*inner.as_ptr().as_raw_ptr(), rstd::move(driver), creation);
    }

    /// Invokes the callable on the calling thread with this runtime's frame allocator.
    /// The callable remains at a stable address until its coroutine frame is destroyed.
    /// The calling thread's current runtime is not changed.
    /// Returned values and detached work must not outlive references to the callable.
    template<typename F>
        requires AwaitableInput<mtp::invoke_result_t<mtp::rm_cvf<F>&>>
    auto spawn_with(F&& function
#if LITO_FEAT_ASYNC_DEBUG
                    ,
                    debug::TaskInfo info     = {},
                    source_location location = source_location::current()
#endif
    ) const -> JoinHandle<await_output_t<mtp::invoke_result_t<mtp::rm_cvf<F>&>>> {
        auto inner = m_inner.upgrade();
        if (! inner) rstd::panic { "RuntimeHandle::spawn_with called after runtime shutdown" };
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        return ::spawn_factory_on(
            *inner.as_ptr().as_raw_ptr(), rstd::forward<F>(function), creation);
    }

    template<typename F>
    auto spawn_blocking(F&& function
#if LITO_FEAT_ASYNC_DEBUG
                        ,
                        debug::TaskInfo info     = {},
                        source_location location = source_location::current()
#endif
    ) const -> io::Result<JoinHandle<mtp::invoke_result_t<F>>> {
        auto inner = m_inner.upgrade();
        if (! inner) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        return ::spawn_blocking_on(
            *inner.as_ptr().as_raw_ptr(), rstd::forward<F>(function), creation);
    }
};

export class Runtime {
    sync::Arc<RuntimeInner>       m_inner;
    Vec<thread::JoinHandle<void>> m_workers;
    Option<Box<RuntimeWorker>>    m_current_worker;

    friend class RuntimeBuilder;
    friend struct RuntimeBuilderConfigAccess;

    explicit Runtime(RuntimeKind             kind,
                     RuntimeConfig           config,
                     usize                   worker_count    = usize(),
                     Option<String>          thread_name     = None(),
                     Option<SharedAllocator> frame_allocator = None())
        : m_inner(sync::Arc<RuntimeInner>::make(kind,
                                                config,
                                                worker_count,
                                                rstd::move(thread_name),
                                                rstd::move(frame_allocator))),
          m_workers(Vec<thread::JoinHandle<void>>::make()),
          m_current_worker(None()) {
        m_inner->init_self(m_inner.downgrade());
    }

    static auto make(RuntimeKind             kind,
                     RuntimeConfig           config,
                     usize                   worker_count,
                     Option<String>          thread_name,
                     Option<SharedAllocator> frame_allocator = None()) -> io::Result<Runtime> {
        auto runtime = Runtime {
            kind, config, worker_count, rstd::move(thread_name), rstd::move(frame_allocator)
        };
#if LITO_FEAT_ASYNC_DEBUG
        if (runtime.m_inner->diagnostic.enabled()) {
            auto session = debug::CollectionSession::start(runtime.m_inner->diagnostic);
            if (session.is_err()) return Err(rstd::move(session).unwrap_err_unchecked());
            runtime.m_inner->diagnostic_session = Some(rstd::move(session).unwrap_unchecked());
        }
#endif
        return Ok(rstd::move(runtime));
    }

    static auto make_thread_pool(usize                   worker_threads,
                                 Option<String> const&   thread_name,
                                 RuntimeConfig           config,
                                 Option<SharedAllocator> frame_allocator) -> io::Result<Runtime> {
        auto created =
            make(RuntimeKind::ThreadPool,
                 config,
                 worker_threads,
                 thread_name.is_some() ? Some(rstd::as<rstd::clone::Clone>(*thread_name).clone())
                                       : None<String>(),
                 rstd::move(frame_allocator));
        if (created.is_err()) return Err(rstd::move(created).unwrap_err_unchecked());
        auto runtime = rstd::move(created).unwrap_unchecked();

        for (rstd::size_t i = 0; i < worker_threads.to_primitive(); ++i) {
            auto inner     = runtime.m_inner.clone();
            auto builder   = thread::builder::Builder::make();
            auto worker_id = RuntimeWorkerId { usize(i) };
            if (thread_name.is_some()) {
                builder.name(rstd::as<rstd::clone::Clone>(*thread_name).clone());
            }

            auto worker = builder.spawn([inner = rstd::move(inner), worker_id] {
                inner->worker_loop(worker_id);
            });

            if (worker.is_err()) {
                auto error = rstd::move(worker).unwrap_err_unchecked();
                runtime.shutdown();
                return Err(rstd::move(error));
            }

            runtime.m_workers.push(rstd::move(worker).unwrap_unchecked());
        }

        auto started = runtime.m_inner->wait_for_startup();
        if (started.is_err()) {
            auto error = rstd::move(started).unwrap_err_unchecked();
            runtime.shutdown();
            return Err(rstd::move(error));
        }
        return Ok(rstd::move(runtime));
    }

    void shutdown() {
        if (! m_inner || ! m_inner->begin_shutdown()) {
            return;
        }

        m_inner->begin_blocking_shutdown();
        m_inner->abort_all_tasks();
        m_inner->join_blocking_pool();
        m_inner->stop_workers();
        while (! m_workers.is_empty()) {
            auto worker = rstd::move(m_workers.pop()).unwrap_unchecked();
            (void)rstd::move(worker).join();
        }
        m_current_worker = None<Box<RuntimeWorker>>();
        m_inner->finish_shutdown();
    }

public:
    Runtime(): Runtime(RuntimeKind::CurrentThread, RuntimeConfig::all()) {}

    Runtime(const Runtime&)            = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) noexcept        = default;
    auto operator=(Runtime&& other) noexcept -> Runtime& {
        if (this != &other) {
            shutdown();
            m_inner          = rstd::move(other.m_inner);
            m_workers        = rstd::move(other.m_workers);
            m_current_worker = rstd::move(other.m_current_worker);
        }
        return *this;
    }
    ~Runtime() { shutdown(); }

#if LITO_FEAT_ASYNC_DEBUG
    auto debug_handle() const -> Option<debug::Handle> {
        if (m_inner->diagnostic_session.is_none()) return None();
        return Some(m_inner->diagnostic_session->handle());
    }
#endif
    auto handle() const -> RuntimeHandle { return RuntimeHandle { m_inner.downgrade() }; }

    auto io_enabled() const -> bool { return m_inner->io_enabled(); }

    auto time_enabled() const -> bool { return m_inner->time_enabled(); }

    template<AwaitableInput A>
    auto spawn(A awaitable
#if LITO_FEAT_ASYNC_DEBUG
               ,
               debug::TaskInfo info     = {},
               source_location location = source_location::current()
#endif
                   ) -> JoinHandle<await_output_t<A>> {
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        auto scope  = FrameAllocatorScope { m_inner->frame_allocator() };
        auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
        return ::spawn_driver_on(*m_inner.as_ptr().as_raw_ptr(), rstd::move(driver), creation);
    }

    template<AwaitableInput A>
    auto spawn_local(A awaitable
#if LITO_FEAT_ASYNC_DEBUG
                     ,
                     debug::TaskInfo info     = {},
                     source_location location = source_location::current()
#endif
                         ) -> JoinHandle<await_output_t<A>> {
        if (m_inner->is_thread_pool()) {
            rstd::panic { "spawn_local is only supported by current-thread runtime" };
        }
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        auto scope  = FrameAllocatorScope { m_inner->frame_allocator() };
        auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
        return ::spawn_driver_on(*m_inner.as_ptr().as_raw_ptr(), rstd::move(driver), creation);
    }

    /// Invokes the callable on the calling thread with this runtime's frame allocator.
    /// The callable remains at a stable address until its coroutine frame is destroyed.
    /// The calling thread's current runtime is not changed.
    /// Returned values and detached work must not outlive references to the callable.
    template<typename F>
        requires AwaitableInput<mtp::invoke_result_t<mtp::rm_cvf<F>&>>
    auto spawn_with(F&& function
#if LITO_FEAT_ASYNC_DEBUG
                    ,
                    debug::TaskInfo info     = {},
                    source_location location = source_location::current()
#endif
                        ) -> JoinHandle<await_output_t<mtp::invoke_result_t<mtp::rm_cvf<F>&>>> {
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        return ::spawn_factory_on(
            *m_inner.as_ptr().as_raw_ptr(), rstd::forward<F>(function), creation);
    }

    template<typename F>
        requires AwaitableInput<mtp::invoke_result_t<mtp::rm_cvf<F>&>>
    auto spawn_local_with(F&& function
#if LITO_FEAT_ASYNC_DEBUG
                          ,
                          debug::TaskInfo info     = {},
                          source_location location = source_location::current()
#endif
                              )
        -> JoinHandle<await_output_t<mtp::invoke_result_t<mtp::rm_cvf<F>&>>> {
        if (m_inner->is_thread_pool())
            rstd::panic { "spawn_local_with is only supported by current-thread runtime" };
#if LITO_FEAT_ASYNC_DEBUG
        return spawn_with(rstd::forward<F>(function), info, location);
#else
        return spawn_with(rstd::forward<F>(function));
#endif
    }

    template<typename F>
    auto spawn_blocking(F&& function
#if LITO_FEAT_ASYNC_DEBUG
                        ,
                        debug::TaskInfo info     = {},
                        source_location location = source_location::current()
#endif
                            ) -> io::Result<JoinHandle<mtp::invoke_result_t<F>>> {
        auto creation = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { info, location };
#endif
        return ::spawn_blocking_on(
            *m_inner.as_ptr().as_raw_ptr(), rstd::forward<F>(function), creation);
    }

    template<AwaitableInput A>
    auto block_on(A awaitable
#if LITO_FEAT_ASYNC_DEBUG
                  ,
                  source_location location = source_location::current()
#endif
                      ) -> await_output_t<A> {
        auto frame_scope = FrameAllocatorScope { m_inner->frame_allocator() };
        auto driver      = make_runtime_driver(into_coro(rstd::move(awaitable)));
        auto creation    = TaskCreationInfo {};
#if LITO_FEAT_ASYNC_DEBUG
        creation = TaskCreationInfo { debug::TaskInfo { "block_on"_str, location }, location };
#endif
        using Output  = await_output_t<A>;
        auto* runtime = m_inner.as_ptr().as_raw_ptr();
        if (m_inner->is_thread_pool()) {
            if (CURRENT_RUNTIME == runtime && has_current_runtime_worker() &&
                current_execution_domain() == ExecutionDomainKind::RuntimeWorker) {
                rstd::panic { "Runtime::block_on cannot run inside its runtime worker" };
            }

            auto joined = JoinState<Output> {};
            auto root   = ScopedTaskStorage<DriverTaskState<Output>> {
                runtime->weak(),
                rstd::move(driver),
                JoinStateOwner<Output>::borrowed(joined),
                creation
            };
            joined.set_task(root.task());
            root.spawn(*runtime);
            auto result = joined.wait();
            if (result.is_err()) {
                rstd::panic { "Runtime::block_on root task was aborted" };
            }

            if constexpr (mtp::is_void<Output>) {
                (void)rstd::move(result).unwrap_unchecked();
                return;
            } else {
                return rstd::move(result).unwrap_unchecked();
            }
        }

        auto attachment = CurrentThreadWorkerAttachment { runtime->m_shared.worker_handle(
            RuntimeWorkerId::current_thread()) };
        auto scope      = RuntimeScope { *runtime };
        if (m_current_worker.is_none()) {
            m_current_worker = Some(Box<RuntimeWorker>::make(
                *runtime, runtime->m_shared.worker_handle(RuntimeWorkerId::current_thread())));
        }
        auto* worker       = m_current_worker->get();
        auto  worker_scope = RuntimeWorkerScope { RuntimeWorkerId::current_thread(), worker };
        if (! worker->poll_initialized()) {
            rstd::panic { "async runtime worker Poll is unavailable or already attached" };
        }
        auto joined = JoinState<Output> {};
        auto root   = ScopedTaskStorage<DriverTaskState<Output>> {
            runtime->weak(), rstd::move(driver), JoinStateOwner<Output>::borrowed(joined), creation
        };
        joined.set_task(root.task());
        root.spawn(*runtime);
        while (! joined.is_ready()) {
            worker->drain_ready();
            if (! joined.is_ready()) {
                worker->wait_for_work();
            }
        }

        auto result = joined.wait();
        if (result.is_err()) {
            rstd::panic { "Runtime::block_on root task was aborted" };
        }
        if constexpr (mtp::is_void<Output>) {
            (void)rstd::move(result).unwrap_unchecked();
            return;
        } else {
            return rstd::move(result).unwrap_unchecked();
        }
    }

    /// Invokes the callable on the calling thread and keeps it alive until the frame is destroyed.
    /// Returned values and detached work must not outlive references to the callable.
    template<typename F>
        requires AwaitableInput<mtp::invoke_result_t<mtp::rm_cvf<F>&>>
    auto block_on_with(F&& function
#if LITO_FEAT_ASYNC_DEBUG
                       ,
                       source_location location = source_location::current()
#endif
                           ) -> await_output_t<mtp::invoke_result_t<mtp::rm_cvf<F>&>> {
        auto scope = FrameAllocatorScope { m_inner->frame_allocator() };
#if LITO_FEAT_ASYNC_DEBUG
        return block_on(function(), location);
#else
        return block_on(function());
#endif
    }
};

export class RuntimeBuilder {
    RuntimeKind             m_kind;
    usize                   m_worker_threads;
    Option<String>          m_thread_name;
    RuntimeConfig           m_config;
    Option<SharedAllocator> m_frame_allocator;

    RuntimeBuilder(RuntimeKind kind, usize worker_threads)
        : m_kind(kind),
          m_worker_threads(worker_threads),
          m_thread_name(None()),
          m_config(RuntimeConfig::disabled()),
          m_frame_allocator(None()) {}

    friend struct RuntimeBuilderConfigAccess;

public:
    static auto current_thread() -> RuntimeBuilder {
        return RuntimeBuilder { RuntimeKind::CurrentThread, usize(1) };
    }

    static auto multi_thread() -> RuntimeBuilder {
        return RuntimeBuilder { RuntimeKind::ThreadPool, usize(1) };
    }

    auto worker_threads(usize n) -> RuntimeBuilder& {
        m_worker_threads = n;
        return *this;
    }

    auto max_blocking_threads(usize n) -> RuntimeBuilder& {
        m_config.max_blocking_threads = n;
        return *this;
    }

    /// Selects the allocator for coroutine frames created in this runtime's execution scope.
    /// Previously created frames keep their allocator; elided frames do not call it.
    /// This allocator must outlive every frame and pending operation that uses it, including
    /// work transferred to another runtime or external executor. If the runtime is its last
    /// owner, all such work and frames must finish before the runtime is destroyed.
    /// Without aligned coroutine allocation support, only default new alignment is guaranteed.
    auto frame_allocator(SharedAllocator allocator) -> RuntimeBuilder& {
        m_frame_allocator = Some(rstd::move(allocator));
        return *this;
    }

    auto thread_name(String name) -> RuntimeBuilder& {
        m_thread_name = Some(rstd::move(name));
        return *this;
    }

#if LITO_FEAT_ASYNC_DEBUG
    auto enable_debug(debug::Options options = {}) -> RuntimeBuilder& {
        m_config.enable_debug  = true;
        m_config.debug_options = options;
        return *this;
    }

#endif
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
        if (! m_config.valid()) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }
        auto frame_allocator = m_frame_allocator.is_some() ? Some(m_frame_allocator->clone())
                                                           : None<SharedAllocator>();
        if (m_kind == RuntimeKind::CurrentThread) {
            return Runtime::make(RuntimeKind::CurrentThread,
                                 m_config,
                                 usize(),
                                 m_thread_name.is_some()
                                     ? Some(rstd::as<rstd::clone::Clone>(*m_thread_name).clone())
                                     : None<String>(),
                                 rstd::move(frame_allocator));
        }

        if (m_worker_threads == usize()) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        }

        return Runtime::make_thread_pool(
            m_worker_threads, m_thread_name, m_config, rstd::move(frame_allocator));
    }
};

struct RuntimeBuilderConfigAccess {
    static void set_io_backend(RuntimeBuilder& builder, IoBackendPreference preference) {
        builder.m_config.io_backend = preference;
    }

    static auto blocking_thread_count(const Runtime& runtime) -> usize {
        return runtime.m_inner->blocking_thread_count();
    }

    static void set_blocking_keep_alive(Runtime& runtime, time::Duration keep_alive) {
        runtime.m_inner->set_blocking_keep_alive(keep_alive);
    }

    static void fail_next_blocking_spawn(Runtime& runtime) {
        runtime.m_inner->fail_next_blocking_spawn();
    }
};

export template<AwaitableInput A>
#if LITO_FEAT_ASYNC_DEBUG
auto block_on(A awaitable, source_location location = source_location::current())
    -> await_output_t<A> {
#else
auto block_on(A awaitable) -> await_output_t<A> {
#endif
    auto runtime = Runtime {};
#if LITO_FEAT_ASYNC_DEBUG
    return runtime.block_on(rstd::move(awaitable), location);
#else
    return runtime.block_on(rstd::move(awaitable));
#endif
}

} // namespace rstd::async
