module rstd:async.bench_tools;
import :async.poll;
import :async.runtime;

extern "C" void rstd_async_bench_set_io_backend(rstd::async::RuntimeBuilder& builder, int backend) {
    if (backend == 1) {
        rstd::async::RuntimeBuilderConfigAccess::set_io_backend(
            builder, rstd::async::IoBackendPreference::NativeCompletionRequired);
    } else if (backend == 2) {
        rstd::async::RuntimeBuilderConfigAccess::set_io_backend(
            builder, rstd::async::IoBackendPreference::ReadinessEmulationRequired);
    }
}
