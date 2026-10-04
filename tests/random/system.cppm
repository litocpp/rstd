module;
#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <errno.h>
#endif
export module rstd:random_tests;
import :sys.random;
import :random;

using namespace rstd::prelude;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;

struct ScriptedRead {
    usize calls {};
    usize fail_after { usize::MAX };
    int   error {};
    bool  interrupt {};
    bool  zero {};
};
auto scripted_read(void* pointer, mut_ref<u8[]> output) -> rstd::io::Result<usize> {
    auto& state = *static_cast<ScriptedRead*>(pointer);
    ++state.calls;
    EXPECT_TRUE(output.len() <= usize(256));
    if (state.interrupt) {
        state.interrupt = false;
        return Err(Error::from_kind(ErrorKind { ErrorKind::Interrupted }));
    }
    if (state.calls > state.fail_after) return Err(Error::from_raw_os_error(i32(state.error)));
    if (state.zero) return Ok(usize());
    auto n = output.len() > usize(7) ? usize(7) : output.len();
    for (usize i {}; i < n; ++i) output[i] = u8(17);
    return Ok(n);
}

TEST(RandomSystem, ShortReadsInterruptsAndGuards) {
    for (usize n :
         array<usize, 6> { usize(), usize(1), usize(255), usize(256), usize(257), usize(1025) }) {
        array<u8, 1027> storage {};
        auto            out = mut_ref<u8[]>::from_raw_parts(storage.data() + 1, n);
        ScriptedRead    state { .interrupt = true };
        ASSERT_TRUE(rstd::sys::random::fill_from(&state, scripted_read, out).is_ok());
        EXPECT_EQ(storage[usize()], u8());
        EXPECT_EQ(storage[n + usize(1)], u8());
        for (usize i {}; i < n; ++i) ASSERT_EQ(out[i], u8(17));
        if (n == usize()) EXPECT_EQ(state.calls, usize());
        auto real = rstd::random::fill_secure(out);
        ASSERT_TRUE(real.is_ok());
        EXPECT_EQ(storage[usize()], u8());
        EXPECT_EQ(storage[n + usize(1)], u8());
    }
    EXPECT_TRUE(rstd::random::random(rstd::random::Standard<u32> {}).is_ok());
}

TEST(RandomSystem, ZeroProgressAndNativeErrors) {
    array<u8, 32> output {};
    ScriptedRead  zero { .zero = true };
    auto result = rstd::sys::random::fill_from(&zero, scripted_read, output.as_mut_slice());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), ErrorKind { ErrorKind::UnexpectedEof });
#if RSTD_OS_LINUX
    for (int code : array<int, 3> { EAGAIN, ENOSYS, EPERM }) {
        for (usize successful : array<usize, 2> { usize(), usize(1) }) {
            ScriptedRead state { .fail_after = successful, .error = code };
            auto         failed =
                rstd::sys::random::fill_from(&state, scripted_read, output.as_mut_slice());
            ASSERT_TRUE(failed.is_err());
            auto error = failed.unwrap_err();
            EXPECT_EQ(error.raw_os_error().unwrap(), i32(code));
            if (code == EAGAIN) EXPECT_EQ(error.kind(), ErrorKind { ErrorKind::WouldBlock });
        }
    }
#endif
}
