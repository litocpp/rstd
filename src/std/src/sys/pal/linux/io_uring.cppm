export module rstd:sys.pal.linux.io_uring;
export import :sys.pal.poll.types;

import :sys.libc;
import :sys.pal.unix.socket;
import rstd.alloc;

namespace libc        = rstd::sys::libc;
namespace unix_socket = rstd::sys::pal::unix::socket;

namespace rstd::sys::pal::linux::io_uring
{

using ::alloc::boxed::Box;
using ::alloc::vec::Vec;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::sys::pal::poll::Event;
using rstd::sys::pal::poll::Operation;
using rstd::sys::pal::poll::OperationKind;
using rstd::sys::pal::poll::SourceKind;

inline constexpr rstd::uint32_t RING_ENTRIES = 256;
inline constexpr rstd::uint64_t CANCEL_KEY {};
inline constexpr rstd::size_t   PROBE_OPERATION_COUNT = 256;

struct OperationProbe {
    rstd::uint8_t           last_op {};
    rstd::uint8_t           ops_len {};
    rstd::uint16_t          reserved {};
    rstd::uint32_t          reserved2[3] {};
    libc::io_uring_probe_op operations[PROBE_OPERATION_COUNT] {};
};

static_assert(__builtin_offsetof(OperationProbe, operations) ==
              __builtin_offsetof(libc::io_uring_probe, ops));

auto probe_required_operations(os::fd::RawFd ring_fd) -> io::Result<empty> {
    auto probe      = OperationProbe {};
    auto registered = libc::io_uring_register(
        ring_fd, libc::IORING_REGISTER_PROBE, &probe, PROBE_OPERATION_COUNT);
    if (registered < 0) return Err(Error::last_os_error());

    constexpr rstd::uint8_t required[] = {
        libc::IORING_OP_READ,         libc::IORING_OP_WRITE,   libc::IORING_OP_RECV,
        libc::IORING_OP_SEND,         libc::IORING_OP_CONNECT, libc::IORING_OP_ACCEPT,
        libc::IORING_OP_ASYNC_CANCEL,
    };
    for (auto opcode : required) {
        bool supported = false;
        for (rstd::size_t i = 0; i < probe.ops_len; ++i) {
            auto const& operation = probe.operations[i];
            if (operation.op == opcode && (operation.flags & libc::IO_URING_OP_SUPPORTED) != 0) {
                supported = true;
                break;
            }
        }
        if (! supported) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }
    }
    return Ok(empty {});
}

class Mapping {
    void*        m_data { nullptr };
    rstd::size_t m_len {};

public:
    Mapping() noexcept = default;
    Mapping(void* data, rstd::size_t len) noexcept: m_data(data), m_len(len) {}
    Mapping(const Mapping&)                    = delete;
    auto operator=(const Mapping&) -> Mapping& = delete;

    Mapping(Mapping&& other) noexcept
        : m_data(rstd::exchange(other.m_data, nullptr)),
          m_len(rstd::exchange(other.m_len, rstd::size_t())) {}

    auto operator=(Mapping&& other) noexcept -> Mapping& {
        if (this != &other) {
            reset();
            m_data = rstd::exchange(other.m_data, nullptr);
            m_len  = rstd::exchange(other.m_len, rstd::size_t());
        }
        return *this;
    }

    ~Mapping() { reset(); }

    static auto map(os::fd::RawFd fd, rstd::size_t len, rstd::uint64_t offset)
        -> io::Result<Mapping> {
        auto* data = libc::mmap(nullptr,
                                len,
                                libc::PROT_READ | libc::PROT_WRITE,
                                libc::MAP_SHARED | libc::MAP_POPULATE,
                                fd,
                                static_cast<libc::off_t>(offset));
        if (data == libc::MAP_FAILED) return Err(Error::last_os_error());
        return Ok(Mapping { data, len });
    }

    void reset() noexcept {
        if (m_data == nullptr) return;
        (void)libc::munmap(m_data, m_len);
        m_data = nullptr;
        m_len  = rstd::size_t();
    }

    auto data() const noexcept -> void* { return m_data; }
};

struct InFlightOperation {
    u64                           key {};
    OperationKind                 kind { OperationKind::Read };
    u32                           flags {};
    unix_socket::NativeSocketAddr address {};
    bool                          cancel_requested { false };

    explicit InFlightOperation(const Operation& operation)
        : key(operation.operation_key()), kind(operation.kind()), flags(operation.flags()) {
        if (operation.kind() == OperationKind::Connect) {
            address = unix_socket::addr_to_native(operation.address());
        }
    }
};

class NativeOperationTable {
    static constexpr rstd::size_t PAGE_CAPACITY = 64;

    struct Page {
        mem::MaybeUninit<InFlightOperation> records[PAGE_CAPACITY];
        rstd::uint32_t                      generations[PAGE_CAPACITY] {};
        bool                                occupied[PAGE_CAPACITY] {};
    };

    Vec<Box<Page>> m_pages;
    usize          m_len {};

    static auto slot_index(u64 key) noexcept -> rstd::uint32_t {
        return static_cast<rstd::uint32_t>(key.to_primitive());
    }

    static auto generation(u64 key) noexcept -> rstd::uint32_t {
        return static_cast<rstd::uint32_t>(key.to_primitive() >> 32);
    }

    auto page_for(rstd::uint32_t slot) -> Page& {
        auto page_index = static_cast<rstd::size_t>(slot) / PAGE_CAPACITY;
        while (m_pages.len().to_primitive() <= page_index) {
            m_pages.push(Box<Page>::make());
        }
        return *m_pages[usize(page_index)];
    }

public:
    NativeOperationTable(): m_pages(Vec<Box<Page>>::make()) {}

    ~NativeOperationTable() {
        if (! is_empty()) rstd::panic { "io_uring native operation table was not drained" };
    }

    auto install(u64 key, InFlightOperation operation) -> InFlightOperation* {
        auto slot = slot_index(key);
        auto gen  = generation(key);
        if (gen == 0) return nullptr;
        auto& page   = page_for(slot);
        auto  offset = static_cast<rstd::size_t>(slot) % PAGE_CAPACITY;
        if (page.occupied[offset]) return nullptr;
        auto& record             = page.records[offset].write(rstd::move(operation));
        page.generations[offset] = gen;
        page.occupied[offset]    = true;
        ++m_len;
        return rstd::addressof(record);
    }

    auto get(u64 key) noexcept -> InFlightOperation* {
        auto slot       = slot_index(key);
        auto page_index = static_cast<rstd::size_t>(slot) / PAGE_CAPACITY;
        if (page_index >= m_pages.len().to_primitive()) return nullptr;
        auto& page   = *m_pages[usize(page_index)];
        auto  offset = static_cast<rstd::size_t>(slot) % PAGE_CAPACITY;
        if (! page.occupied[offset] || page.generations[offset] != generation(key)) return nullptr;
        return page.records[offset].as_mut_ptr();
    }

    auto take(u64 key) -> Option<InFlightOperation> {
        auto* record = get(key);
        if (record == nullptr) return None<InFlightOperation>();
        auto  slot       = slot_index(key);
        auto  page_index = static_cast<rstd::size_t>(slot) / PAGE_CAPACITY;
        auto& page       = *m_pages[usize(page_index)];
        auto  offset     = static_cast<rstd::size_t>(slot) % PAGE_CAPACITY;
        auto  result     = rstd::move(page.records[offset]).assume_init();
        page.records[offset].assume_init_drop();
        page.occupied[offset] = false;
        --m_len;
        return Some(rstd::move(result));
    }

    template<typename F>
    void for_each(F&& function) {
        for (rstd::size_t page_index = 0; page_index < m_pages.len().to_primitive(); ++page_index) {
            auto& page = *m_pages[usize(page_index)];
            for (rstd::size_t offset = 0; offset < PAGE_CAPACITY; ++offset) {
                if (page.occupied[offset]) function(*page.records[offset].as_mut_ptr());
            }
        }
    }

    auto is_empty() const noexcept -> bool { return m_len == usize(); }
};

export class Driver {
    os::fd::OwnedFd      m_ring_fd;
    os::fd::OwnedFd      m_event_fd;
    Mapping              m_sq_ring;
    Mapping              m_cq_ring;
    Mapping              m_sqes_mapping;
    rstd::uint32_t*      m_sq_head { nullptr };
    rstd::uint32_t*      m_sq_tail { nullptr };
    rstd::uint32_t*      m_sq_mask { nullptr };
    rstd::uint32_t*      m_sq_entries { nullptr };
    rstd::uint32_t*      m_sq_array { nullptr };
    libc::io_uring_sqe*  m_sqes { nullptr };
    rstd::uint32_t*      m_cq_head { nullptr };
    rstd::uint32_t*      m_cq_tail { nullptr };
    rstd::uint32_t*      m_cq_mask { nullptr };
    libc::io_uring_cqe*  m_cqes { nullptr };
    rstd::uint32_t       m_pending_submissions {};
    NativeOperationTable m_operations;

    static auto load_acquire(rstd::uint32_t* value) noexcept -> rstd::uint32_t {
        return __atomic_load_n(value, __ATOMIC_ACQUIRE);
    }

    static auto load_relaxed(rstd::uint32_t* value) noexcept -> rstd::uint32_t {
        return __atomic_load_n(value, __ATOMIC_RELAXED);
    }

    static void store_release(rstd::uint32_t* value, rstd::uint32_t next) noexcept {
        __atomic_store_n(value, next, __ATOMIC_RELEASE);
    }

    auto submit_pending() -> io::Result<empty> {
        while (m_pending_submissions != 0) {
            long submitted {};
            do {
                submitted = libc::io_uring_enter(
                    m_ring_fd.as_raw_fd(), m_pending_submissions, 0, 0, nullptr, 0);
            } while (submitted < 0 && libc::get_errno() == libc::EINTR);
            if (submitted < 0) return Err(Error::last_os_error());
            if (submitted == 0) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::WouldBlock }));
            }
            m_pending_submissions -= static_cast<rstd::uint32_t>(submitted);
        }
        return Ok(empty {});
    }

    auto push(libc::io_uring_sqe submission) -> io::Result<empty> {
        auto head = load_acquire(m_sq_head);
        auto tail = load_relaxed(m_sq_tail);
        if (tail - head >= *m_sq_entries) {
            auto submitted = submit_pending();
            if (submitted.is_err()) return submitted;
            head = load_acquire(m_sq_head);
            tail = load_relaxed(m_sq_tail);
            if (tail - head >= *m_sq_entries) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::ResourceBusy }));
            }
        }

        auto index        = tail & *m_sq_mask;
        m_sqes[index]     = submission;
        m_sq_array[index] = index;
        store_release(m_sq_tail, tail + 1);
        ++m_pending_submissions;
        return Ok(empty {});
    }

    auto cancel(InFlightOperation& operation) -> io::Result<empty> {
        if (operation.cancel_requested) return Ok(empty {});
        auto submission         = libc::io_uring_sqe {};
        submission.opcode       = libc::IORING_OP_ASYNC_CANCEL;
        submission.fd           = -1;
        submission.addr         = operation.key.to_primitive();
        submission.user_data    = CANCEL_KEY;
        submission.cancel_flags = 0;
        auto queued             = push(submission);
        if (queued.is_ok()) operation.cancel_requested = true;
        return queued;
    }

public:
    Driver(os::fd::OwnedFd              ring_fd,
           os::fd::OwnedFd              event_fd,
           Mapping                      sq_ring,
           Mapping                      cq_ring,
           Mapping                      sqes_mapping,
           const libc::io_uring_params& params)
        : m_ring_fd(rstd::move(ring_fd)),
          m_event_fd(rstd::move(event_fd)),
          m_sq_ring(rstd::move(sq_ring)),
          m_cq_ring(rstd::move(cq_ring)),
          m_sqes_mapping(rstd::move(sqes_mapping)),
          m_operations() {
        auto* sq_base = static_cast<char*>(m_sq_ring.data());
        auto* cq_base = params.features & libc::IORING_FEAT_SINGLE_MMAP
                            ? sq_base
                            : static_cast<char*>(m_cq_ring.data());
        m_sq_head     = reinterpret_cast<rstd::uint32_t*>(sq_base + params.sq_off.head);
        m_sq_tail     = reinterpret_cast<rstd::uint32_t*>(sq_base + params.sq_off.tail);
        m_sq_mask     = reinterpret_cast<rstd::uint32_t*>(sq_base + params.sq_off.ring_mask);
        m_sq_entries  = reinterpret_cast<rstd::uint32_t*>(sq_base + params.sq_off.ring_entries);
        m_sq_array    = reinterpret_cast<rstd::uint32_t*>(sq_base + params.sq_off.array);
        m_sqes        = static_cast<libc::io_uring_sqe*>(m_sqes_mapping.data());
        m_cq_head     = reinterpret_cast<rstd::uint32_t*>(cq_base + params.cq_off.head);
        m_cq_tail     = reinterpret_cast<rstd::uint32_t*>(cq_base + params.cq_off.tail);
        m_cq_mask     = reinterpret_cast<rstd::uint32_t*>(cq_base + params.cq_off.ring_mask);
        m_cqes        = reinterpret_cast<libc::io_uring_cqe*>(cq_base + params.cq_off.cqes);
    }

    Driver(const Driver&)                    = delete;
    auto operator=(const Driver&) -> Driver& = delete;
    Driver(Driver&&)                         = delete;
    auto operator=(Driver&&) -> Driver&      = delete;

    ~Driver() { m_ring_fd = os::fd::OwnedFd {}; }

    static auto make() -> io::Result<Box<Driver>> {
        auto params = libc::io_uring_params {};
        auto raw_fd = libc::io_uring_setup(RING_ENTRIES, &params);
        if (raw_fd < 0) return Err(Error::last_os_error());
        auto ring_fd = os::fd::OwnedFd::from_raw_fd(static_cast<os::fd::RawFd>(raw_fd));

        auto probed = probe_required_operations(ring_fd.as_raw_fd());
        if (probed.is_err()) return Err(rstd::move(probed).unwrap_err_unchecked());

        auto sq_len = static_cast<rstd::size_t>(params.sq_off.array) +
                      static_cast<rstd::size_t>(params.sq_entries) * sizeof(rstd::uint32_t);
        auto cq_len = static_cast<rstd::size_t>(params.cq_off.cqes) +
                      static_cast<rstd::size_t>(params.cq_entries) * sizeof(libc::io_uring_cqe);
        if (params.features & libc::IORING_FEAT_SINGLE_MMAP) {
            sq_len = sq_len < cq_len ? cq_len : sq_len;
        }

        auto sq_ring = Mapping::map(ring_fd.as_raw_fd(), sq_len, libc::IORING_OFF_SQ_RING);
        if (sq_ring.is_err()) return Err(rstd::move(sq_ring).unwrap_err_unchecked());

        auto cq_ring = Mapping {};
        if (! (params.features & libc::IORING_FEAT_SINGLE_MMAP)) {
            auto mapped = Mapping::map(ring_fd.as_raw_fd(), cq_len, libc::IORING_OFF_CQ_RING);
            if (mapped.is_err()) return Err(rstd::move(mapped).unwrap_err_unchecked());
            cq_ring = rstd::move(mapped).unwrap_unchecked();
        }

        auto sqes_len = static_cast<rstd::size_t>(params.sq_entries) * sizeof(libc::io_uring_sqe);
        auto sqes     = Mapping::map(ring_fd.as_raw_fd(), sqes_len, libc::IORING_OFF_SQES);
        if (sqes.is_err()) return Err(rstd::move(sqes).unwrap_err_unchecked());

        auto event_raw = libc::eventfd(0, libc::EFD_NONBLOCK | libc::EFD_CLOEXEC);
        if (event_raw < 0) return Err(Error::last_os_error());
        auto event_fd   = os::fd::OwnedFd::from_raw_fd(event_raw);
        auto registered = libc::io_uring_register(
            ring_fd.as_raw_fd(), libc::IORING_REGISTER_EVENTFD, &event_raw, 1);
        if (registered < 0) return Err(Error::last_os_error());

        return Ok(Box<Driver>::make(rstd::move(ring_fd),
                                    rstd::move(event_fd),
                                    rstd::move(sq_ring).unwrap_unchecked(),
                                    rstd::move(cq_ring),
                                    rstd::move(sqes).unwrap_unchecked(),
                                    params));
    }

    auto event_fd() const noexcept -> os::fd::RawFd { return m_event_fd.as_raw_fd(); }

    auto submit_operation(Operation operation) -> io::Result<empty> {
        if (operation.handle() == os::fd::INVALID_RAW_FD ||
            m_operations.get(operation.operation_key()) != nullptr ||
            operation.len().to_primitive() > rstd::uint32_t(-1)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (operation.source_kind() != SourceKind::Socket &&
            operation.kind() != OperationKind::Read && operation.kind() != OperationKind::Write) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }

        auto* record =
            m_operations.install(operation.operation_key(), InFlightOperation { operation });
        if (record == nullptr) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        auto submission      = libc::io_uring_sqe {};
        submission.fd        = operation.handle();
        submission.len       = static_cast<rstd::uint32_t>(operation.len().to_primitive());
        submission.user_data = operation.operation_key().to_primitive();

        if (operation.kind() == OperationKind::Read) {
            submission.opcode = operation.source_kind() == SourceKind::Socket
                                    ? libc::IORING_OP_RECV
                                    : libc::IORING_OP_READ;
            submission.addr   = reinterpret_cast<rstd::uintptr_t>(operation.mutable_data());
            submission.off    = operation.source_kind() == SourceKind::Socket ? rstd::uint64_t()
                                : operation.offset().is_some() ? operation.offset()->to_primitive()
                                                               : rstd::uint64_t(-1);
            submission.rw_flags = operation.flags().to_primitive();
        } else if (operation.kind() == OperationKind::Write) {
            submission.opcode = operation.source_kind() == SourceKind::Socket
                                    ? libc::IORING_OP_SEND
                                    : libc::IORING_OP_WRITE;
            submission.addr   = reinterpret_cast<rstd::uintptr_t>(operation.const_data());
            submission.off    = operation.source_kind() == SourceKind::Socket ? rstd::uint64_t()
                                : operation.offset().is_some() ? operation.offset()->to_primitive()
                                                               : rstd::uint64_t(-1);
            submission.rw_flags = operation.flags().to_primitive();
            if (operation.source_kind() == SourceKind::Socket) {
                submission.msg_flags |= libc::MSG_NOSIGNAL;
            }
        } else if (operation.kind() == OperationKind::Connect) {
            submission.opcode = libc::IORING_OP_CONNECT;
            submission.addr   = reinterpret_cast<rstd::uintptr_t>(&record->address.storage);
            submission.off    = record->address.len;
        } else {
            submission.opcode       = libc::IORING_OP_ACCEPT;
            submission.addr         = 0;
            submission.off          = 0;
            submission.accept_flags = libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC;
        }

        auto queued = push(submission);
        if (queued.is_err()) {
            (void)m_operations.take(operation.operation_key());
            return queued;
        }
        return Ok(empty {});
    }

    auto cancel_operation(u64 operation_key) -> io::Result<empty> {
        auto* operation = m_operations.get(operation_key);
        if (operation == nullptr) return Ok(empty {});
        return cancel(*operation);
    }

    auto begin_shutdown() noexcept -> empty {
        m_operations.for_each([this](InFlightOperation& operation) {
            (void)cancel(operation);
        });
        (void)submit_pending();
        return empty {};
    }

    auto flush() -> io::Result<empty> { return submit_pending(); }

    auto drain(Vec<Event>& events) -> bool {
        auto head  = load_relaxed(m_cq_head);
        auto tail  = load_acquire(m_cq_tail);
        auto ready = head != tail;
        while (head != tail) {
            auto& completion = m_cqes[head & *m_cq_mask];
            auto  key        = u64(completion.user_data);
            if (key != u64(CANCEL_KEY)) {
                auto operation = m_operations.take(key);
                if (operation.is_some()) {
                    auto record = rstd::move(operation).unwrap_unchecked();
                    if (completion.res < 0) {
                        events.push(Event::completion_error(
                            key, io::error::RawOsError(-completion.res), record.flags));
                    } else if (record.kind == OperationKind::Accept) {
                        events.push(Event::completion_resource(key, completion.res, record.flags));
                    } else {
                        events.push(Event::completion(key, isize(completion.res), record.flags));
                    }
                }
            }
            ++head;
        }
        store_release(m_cq_head, head);
        return ready;
    }

    auto has_pending_operations() const noexcept -> bool { return ! m_operations.is_empty(); }
};

} // namespace rstd::sys::pal::linux::io_uring
