export module rstd.thread:scoped;
export import :thread;
export import rstd.core;

using rstd::sync::atomic::Atomic;

namespace rstd::thread::scpoed
{

struct ScopeData {
    Atomic<usize> num_running_threads;
    Atomic<bool>  a_thread_panicked;
    Thread        main_thread;
};

} // namespace rstd::thread::scpoed