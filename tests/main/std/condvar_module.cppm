export module rstd.tests.std_condvar_module;
import rstd.sys.sync.condvar;

export auto std_condvar_module() -> bool {
    auto mutex   = rstd::sys::sync::mutex::Mutex::make();
    auto condvar = rstd::sys::sync::condvar::Condvar::make();
    mutex.lock();
    (void)condvar.wait_timeout(mutex, rstd::time::Duration::from_millis(rstd::u64()));
    auto held = ! mutex.try_lock();
    mutex.unlock();
    condvar.notify_one();
    condvar.notify_all();
    return held;
}
