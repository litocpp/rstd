export module rstd.thread:lifecycle;
export import :thread;
export import :scoped;
export import :forward;

using rstd::thread::scoped::ScopeData;
namespace rstd::thread::lifecycle
{
struct ThreadInit {
    Thread handle;
    // Box<dyn FnOnce() + Send> rust_start;
};

template<typename T>
struct Packet {
    Option<Arc<ScopeData>> scope;
    Option<Result<T>> result;
};

} // namespace rstd::thread::lifecycle