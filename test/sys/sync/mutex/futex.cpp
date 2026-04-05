#include <gtest/gtest.h>
#include <thread>

import rstd;
using rstd::sys::sync::mutex::futex::Mutex;

#define RSTD_TEST_GROUP MutexFutex
#include "common.hpp"
