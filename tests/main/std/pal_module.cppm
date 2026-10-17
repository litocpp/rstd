module;
#include <rstd/macro.hpp>

export module rstd.tests.std_pal_module;

import rstd.core;
import rstd.sys.pal;

export auto std_pal_time() -> bool {
    using namespace rstd;
    namespace pal       = sys::pal;
    auto const duration = time::Duration::from_secs(u64(1));
    auto const instant  = pal::Instant::now();
    auto const later    = instant.checked_add_duration(duration);
    if (! later || later->duration_since(instant) != duration) return false;
    if (later->checked_sub_duration(duration) != Some(instant)) return false;
    auto const epoch = pal::SystemTime::unix_epoch().as_unix_time();
    if (epoch.seconds != i64() || epoch.nanoseconds != u32()) return false;
    auto const before_epoch = pal::SystemTime::from_unix_time(i64(-1), u32(200));
    if (! before_epoch) return false;
    auto const timestamp = before_epoch->as_unix_time();
    return timestamp.seconds == i64(-1) && timestamp.nanoseconds == u32(200) &&
           pal::SystemTime::from_unix_time(i64(), u32(1'000'000'000)).is_none();
}

export auto std_pal_sync() -> bool {
    using namespace rstd;
    namespace pal = sys::pal;
#if ! RSTD_OS_UNKNOWN
    auto mutex = pal::Mutex::make();
    if (! mutex.try_lock()) return false;
    mutex.unlock();
    mutex.lock();
    auto condvar = pal::Condvar::make();
    condvar.notify_one();
    condvar.notify_all();
    (void)condvar.wait_timeout(mutex, time::Duration());
    mutex.unlock();
#endif
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
    pal::futex::Futex word { u32(1) };
    if (! pal::futex::futex_wait(&word, u32(), Some(time::Duration()))) return false;
    (void)pal::futex::futex_wake(&word);
    pal::futex::futex_wake_all(&word);
#endif
    return true;
}

export auto std_pal_process_state() -> bool {
#if ! RSTD_OS_UNKNOWN
    namespace pal = rstd::sys::pal;
    if (pal::getpid_internal() == rstd::u32()) return false;
    auto const  saved       = pal::args_argc_argv();
    const char* arguments[] = { "rstd-pal-module", "argument" };
    pal::args_capture(2, arguments);
    auto const observed = pal::args_argc_argv();
    auto const matches  = observed.argc == 2 && observed.argv == arguments;
    pal::args_capture(saved.argc, saved.argv);
    return matches;
#else
    return true;
#endif
}
