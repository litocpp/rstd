module;
#include <rstd/macro.hpp>

module rstd_benches:async.support;
import rstd;
import rstd_benches;
using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

auto runtime_failure(const io::error::Error& error) -> rstd_bench::CaseRunResult {
    auto reason = rstd::format("runtime setup failed: {}", error);
    if (error.kind() == io::error::ErrorKind { io::error::ErrorKind::Unsupported })
        return rstd_bench::CaseRunResult::Skipped(rstd::move(reason));
    return rstd_bench::failed(rstd::move(reason));
}

extern "C" void rstd_async_bench_set_io_backend(async::RuntimeBuilder& builder, int backend);

enum class IoBackend
{
    Auto,
    NativeCompletion,
    ReadinessEmulation,
};

auto make_io_runtime(IoBackend backend) -> io::Result<async::Runtime> {
    auto builder = async::RuntimeBuilder::current_thread();
    builder.enable_io();
    rstd_async_bench_set_io_backend(builder, static_cast<int>(backend));
    return builder.build();
}

auto make_thread_pool_io_runtime(IoBackend backend, usize worker_threads)
    -> io::Result<async::Runtime> {
    auto builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(worker_threads).enable_io();
    rstd_async_bench_set_io_backend(builder, static_cast<int>(backend));
    return builder.build();
}

inline constexpr rstd::size_t LOOPBACK_BATCH       = 64;
inline constexpr rstd::size_t LOOPBACK_CONCURRENCY = 4;
inline constexpr rstd::size_t KIB                  = 1024;
