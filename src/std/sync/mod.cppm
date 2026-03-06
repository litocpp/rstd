export module rstd:sync;
export import :sync.poison;
export import :sync.mutex;
export import :sync.mpsc;
import rstd.alloc;

namespace rstd::sync
{
export {
    using ::alloc::sync::Arc;
    using ::alloc::sync::ArcRaw;
    using ::alloc::sync::Weak;
}
} // namespace rstd::sync