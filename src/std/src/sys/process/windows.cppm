module;
#include <rstd/macro.hpp>

export module rstd.sys.process.windows;
#if RSTD_OS_WINDOWS
import rstd.core;
import rstd.io.error;
import rstd.os.handle;

export namespace rstd::sys::process
{

struct ChildPipe {
    os::handle::OwnedHandle parent;
    os::handle::OwnedHandle child;
};

auto child_pipe(bool child_reads) -> io::Result<ChildPipe>;

struct SpawnedProcess {
    os::handle::OwnedHandle handle;
    u32                     id;
};

auto spawn_process(wchar_t*              command,
                   void*                 environment,
                   const wchar_t*        directory,
                   os::handle::RawHandle input,
                   os::handle::RawHandle output,
                   os::handle::RawHandle error) -> io::Result<SpawnedProcess>;

class ProcessWait {
    void* state_ {};
    explicit ProcessWait(void* state): state_(state) {}
    void reset() noexcept;

public:
    using Callback                  = void (*)(void*) noexcept;
    ProcessWait(const ProcessWait&) = delete;
    ProcessWait(ProcessWait&& other) noexcept: state_(rstd::exchange(other.state_, nullptr)) {}
    auto operator=(ProcessWait&& other) noexcept -> ProcessWait& {
        if (this != &other) {
            reset();
            state_ = rstd::exchange(other.state_, nullptr);
        }
        return *this;
    }
    ~ProcessWait();
    // Keep the context alive until destruction, which waits for callbacks to finish.
    // Destroy the registration outside its callback and without locks needed by that callback.
    static auto make(os::handle::BorrowedHandle process, Callback callback, void* context)
        -> io::Result<ProcessWait>;
};

} // namespace rstd::sys::process
#endif
