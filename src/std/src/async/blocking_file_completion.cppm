module;
#include <rstd/macro.hpp>

export module rstd:async.blocking_file_completion;
import :async.blocking_pool;
import :io;
import :os.fd;
import :sync;
import :sys.fs;
import :sys.pal.poll;
import rstd.alloc;
import rstd.core;

using ::alloc::vec::Vec;
using rstd::sync::Arc;
namespace pal_poll = rstd::sys::pal::poll;

namespace rstd::async
{

enum class BlockingFileOperationKind
{
    Read,
    Write,
};

struct BlockingFileOperation {
    BlockingFileOperationKind kind;
    os::fd::RawFd             fd;
    u64                       source_key;
    u64                       operation_key;
    void*                     data;
    usize                     len;
    Option<u64>               offset;
    u32                       flags;
};

struct BlockingFileCompletion {
    u64               operation_key;
    io::Result<usize> result;
    u32               flags;
};

namespace blocking_file_completion
{

struct CompletionState {
    sync::Mutex<Vec<BlockingFileCompletion>> completions;
    pal_poll::PollWake                       wake;

    explicit CompletionState(pal_poll::PollWake wake)
        : completions(Vec<BlockingFileCompletion>::make()), wake(rstd::move(wake)) {}

    void publish(BlockingFileCompletion completion) {
        {
            auto pending = completions.lock().unwrap_unchecked();
            pending->push(rstd::move(completion));
        }
        (void)wake.wake();
    }

    auto take() -> Vec<BlockingFileCompletion> {
        auto pending = completions.lock().unwrap_unchecked();
        auto result  = rstd::move(*pending);
        *pending     = Vec<BlockingFileCompletion>::make();
        return result;
    }
};

struct ActiveOperation {
    u64                     operation_key;
    u64                     source_key;
    BlockingJobCancellation cancellation;
};

auto execute(BlockingFileOperation operation) -> io::Result<usize> {
#if RSTD_OS_LINUX
    if (operation.kind == BlockingFileOperationKind::Read) {
        auto buffer =
            mut_ref<byte[]>::from_raw_parts(static_cast<byte*>(operation.data), operation.len);
        return operation.offset.is_some()
                   ? sys::fs::read_at(operation.fd, buffer, *operation.offset)
                   : sys::fs::read(operation.fd, buffer);
    }
    auto buffer =
        slice<byte>::from_raw_parts(static_cast<const byte*>(operation.data), operation.len);
    return operation.offset.is_some() ? sys::fs::write_at(operation.fd, buffer, *operation.offset)
                                      : sys::fs::write(operation.fd, buffer);
#else
    (void)operation;
    return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
}

} // namespace blocking_file_completion

class BlockingFileCompletionDriver {
    BlockingSpawner                                m_spawner;
    Arc<blocking_file_completion::CompletionState> m_completions;
    Vec<blocking_file_completion::ActiveOperation> m_active;

    auto active_index(u64 operation_key) const -> Option<usize> {
        for (auto index = usize(); index < m_active.len(); ++index) {
            if (m_active[index].operation_key == operation_key) return Some(index);
        }
        return None<usize>();
    }

public:
    BlockingFileCompletionDriver(BlockingSpawner spawner, pal_poll::PollWake wake)
        : m_spawner(rstd::move(spawner)),
          m_completions(Arc<blocking_file_completion::CompletionState>::make(rstd::move(wake))),
          m_active(Vec<blocking_file_completion::ActiveOperation>::make()) {}

    BlockingFileCompletionDriver(const BlockingFileCompletionDriver&)                    = delete;
    auto operator=(const BlockingFileCompletionDriver&) -> BlockingFileCompletionDriver& = delete;
    BlockingFileCompletionDriver(BlockingFileCompletionDriver&&) noexcept                = default;
    auto operator=(BlockingFileCompletionDriver&&) noexcept
        -> BlockingFileCompletionDriver& = default;

    auto submit(BlockingFileOperation operation) -> io::Result<empty> {
        auto operation_key = operation.operation_key;
        auto source_key    = operation.source_key;
        auto flags         = operation.flags;
        auto completions   = m_completions.clone();
        auto job           = BlockingJob::make(
            [operation = rstd::move(operation), completions = completions.clone()]() mutable {
                auto key    = operation.operation_key;
                auto flags  = operation.flags;
                auto result = blocking_file_completion::execute(rstd::move(operation));
                completions->publish(BlockingFileCompletion { key, rstd::move(result), flags });
            },
            [operation_key, flags, completions = rstd::move(completions)]() mutable {
                completions->publish(BlockingFileCompletion {
                    operation_key,
                    Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted })),
                    flags,
                });
            });
        auto submitted = m_spawner.submit(rstd::move(job));
        if (submitted.is_err()) {
            return Err(rstd::move(submitted).unwrap_err_unchecked());
        }
        m_active.push(blocking_file_completion::ActiveOperation {
            operation_key, source_key, rstd::move(submitted).unwrap_unchecked() });
        return Ok(empty {});
    }

    void cancel(u64 operation_key) {
        auto index = active_index(operation_key);
        if (index.is_some()) {
            (void)m_active[*index].cancellation.cancel();
        }
    }

    void cancel_all() {
        for (auto index = usize(); index < m_active.len(); ++index) {
            (void)m_active[index].cancellation.cancel();
        }
    }

    auto has_active() const noexcept -> bool { return ! m_active.is_empty(); }

    auto source_active(u64 source_key) const noexcept -> bool {
        for (auto index = usize(); index < m_active.len(); ++index) {
            if (m_active[index].source_key == source_key) return true;
        }
        return false;
    }

    auto drain() -> Vec<BlockingFileCompletion> {
        auto pending = m_completions->take();
        auto result  = Vec<BlockingFileCompletion>::with_capacity(pending.len());
        for (auto index = usize(); index < pending.len(); ++index) {
            auto active = active_index(pending[index].operation_key);
            if (active.is_none()) continue;
            (void)m_active.remove(*active);
            result.push(rstd::move(pending[index]));
        }
        return result;
    }
};

} // namespace rstd::async
