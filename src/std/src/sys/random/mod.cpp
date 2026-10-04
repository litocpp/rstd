module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <errno.h>
#include <sys/random.h>
#elif RSTD_VENDOR_APPLE
#include <CommonCrypto/CommonRandom.h>
#elif RSTD_OS_WINDOWS
#include <windows.h>
#endif
module rstd;
import :sys.random;
import :io.error;

using namespace rstd::prelude;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;

auto rstd::sys::random::fill_from(void* state, Read read, mut_ref<u8[]> output)
    -> rstd::io::Result<empty> {
    usize offset {};
    while (offset < output.len()) {
        auto count = output.len() - offset;
        if (count > usize(256)) count = usize(256);
        auto chunk =
            mut_ref<u8[]>::from_raw_parts(output.as_raw_ptr() + offset.to_primitive(), count);
        auto result = read(state, chunk);
        if (result.is_err()) {
            auto error = rstd::move(result).unwrap_err();
            if (error.kind() == ErrorKind { ErrorKind::Interrupted }) continue;
            return Err(rstd::move(error));
        }
        auto written = result.unwrap();
        if (written == usize())
            return Err(Error::from_kind(ErrorKind { ErrorKind::UnexpectedEof }));
        if (written > count) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidData }));
        offset += written;
    }
    return Ok(empty {});
}

#if RSTD_OS_LINUX && (! defined(__ANDROID__) || __ANDROID_API__ >= 28)
auto read_os_random(void*, mut_ref<u8[]> output) -> rstd::io::Result<usize> {
    auto count = ::getrandom(output.as_raw_ptr(), output.len().to_primitive(), GRND_NONBLOCK);
    if (count < 0) return Err(Error::from_raw_os_error(i32(errno)));
    return Ok(usize(count));
}
#endif

auto rstd::sys::random::fill_bytes(mut_ref<u8[]> output) -> rstd::io::Result<empty> {
    if (output.is_empty()) return Ok(empty {});
#if RSTD_OS_LINUX && (! defined(__ANDROID__) || __ANDROID_API__ >= 28)
    return fill_from(nullptr, read_os_random, output);
#elif RSTD_VENDOR_APPLE
    auto status = ::CCRandomGenerateBytes(output.as_raw_ptr(), output.len().to_primitive());
    if (status != kCCSuccess) return Err(Error::from_kind(ErrorKind { ErrorKind::Other }));
    return Ok(empty {});
#elif RSTD_OS_WINDOWS
    // System-only loading keeps the optional Windows 8 API out of the import table.
    auto library = ::LoadLibraryExW(L"bcryptprimitives.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (! library) return Err(Error::from_raw_os_error(i32(::GetLastError())));
    using ProcessPrng = BOOL(WINAPI*)(PBYTE, SIZE_T);
    auto generate     = reinterpret_cast<ProcessPrng>(::GetProcAddress(library, "ProcessPrng"));
    if (! generate) {
        ::FreeLibrary(library);
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }
    auto success =
        generate(reinterpret_cast<PBYTE>(output.as_raw_ptr()), output.len().to_primitive());
    ::FreeLibrary(library);
    if (! success) return Err(Error::from_kind(ErrorKind { ErrorKind::Other }));
    return Ok(empty {});
#else
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}
