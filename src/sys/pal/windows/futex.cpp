#ifdef _WIN32
module;
#    include <windows.h>
module rstd.sys;
import :pal.windows.futex;

namespace rstd::pal::windows
{
bool wait_on_address(const void* address, const void* compare, size_t size, DWORD timeout) {
    return WaitOnAddress(const_cast<void*>(address), const_cast<void*>(compare), size, timeout) ==
           TRUE;
}

void wake_by_address_single(const void* address) {
    WakeByAddressSingle(const_cast<void*>(address));
}

void wake_by_address_all(const void* address) { WakeByAddressAll(const_cast<void*>(address)); }

template<typename T>
bool futex_wait(const std::atomic<T>* futex, T expected, std::chrono::milliseconds timeout) {
    DWORD timeout_ms = timeout == std::chrono::milliseconds::max()
                           ? INFINITE
                           : static_cast<DWORD>(timeout.count());

    bool result = wait_on_address(futex, &expected, sizeof(T), timeout_ms);
    return result || GetLastError() != ERROR_TIMEOUT;
}

template<typename T>
bool futex_wake(const std::atomic<T>* futex) {
    wake_by_address_single(futex);
    return false;
}

template<typename T>
void futex_wake_all(const std::atomic<T>* futex) {
    wake_by_address_all(futex);
}

// Explicit template instantiations for common types
template bool futex_wait<uint32_t>(const std::atomic<uint32_t>*, uint32_t,
                                   std::chrono::milliseconds);
template bool futex_wait<uint8_t>(const std::atomic<uint8_t>*, uint8_t, std::chrono::milliseconds);
template bool futex_wake<uint32_t>(const std::atomic<uint32_t>*);
template bool futex_wake<uint8_t>(const std::atomic<uint8_t>*);
template void futex_wake_all<uint32_t>(const std::atomic<uint32_t>*);
template void futex_wake_all<uint8_t>(const std::atomic<uint8_t>*);
} // namespace rstd::pal::windows

#endif