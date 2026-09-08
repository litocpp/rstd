module;
#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>
#include <thread>
#include <vector>

module rstd;
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
import :sys.sync.mutex.futex;

namespace rstd_mutex_futex_test
{
using rstd::sys::sync::mutex::futex::Mutex;

#define RSTD_TEST_GROUP MutexFutex
#include "common.hpp"
} // namespace rstd_mutex_futex_test
#endif
