#include <rstd/test/gtest.hpp>

import rstd.tests.std_leaf_modules;
import rstd.tests.std_process_module;
import rstd.tests.std_pal_module;

import rstd.tests.std_io_error_module;
import rstd.tests.std_socket_addr_module;
import rstd.tests.std_fd_module;
import rstd.tests.std_socket_module;
import rstd.tests.std_handle_module;

import rstd.tests.std_io_module;
import rstd.tests.std_sys_socket_module;
import rstd.tests.std_thread_module;
import rstd.tests.std_sys_thread_module;
import rstd.tests.std_condvar_module;
import rstd;

TEST(StdModules, DirectLibcImports) {
    EXPECT_TRUE(std_leaf_memory());
    EXPECT_TRUE(std_leaf_platform());
}

TEST(StdModules, DirectSystemMemoryImport) {
    EXPECT_TRUE(std_leaf_mapping());
}

TEST(StdModules, DirectProcessImport) {
    EXPECT_TRUE(std_process_module_errors());
}

TEST(StdModules, DirectPalTimeImport) {
    EXPECT_TRUE(std_pal_time());
}

TEST(StdModules, DirectPalSynchronizationImport) {
    EXPECT_TRUE(std_pal_sync());
}

TEST(StdModules, DirectPalProcessStateImport) {
    EXPECT_TRUE(std_pal_process_state());
}

TEST(StdModules, DirectIoErrorImport) {
    EXPECT_TRUE(std_io_error_module());
}

TEST(StdModules, DirectSocketAddrImport) {
    EXPECT_TRUE(std_socket_addr_module());
}

TEST(StdModules, DirectFdImport) {
    EXPECT_TRUE(std_fd_module());
}

TEST(StdModules, DirectSocketImport) {
    EXPECT_TRUE(std_socket_module());
}

TEST(StdModules, DirectHandleImport) {
    EXPECT_TRUE(std_handle_module());
}

TEST(StdModules, DirectIoImport) {
    EXPECT_TRUE(std_io_module());
}

TEST(StdModules, DirectSysSocketImport) {
    EXPECT_TRUE(std_sys_socket_module());
}

TEST(StdModules, DirectThreadImport) {
    EXPECT_TRUE(std_thread_module());
}

TEST(StdModules, DirectSysThreadImport) {
    EXPECT_TRUE(std_sys_thread_module());
}

TEST(StdModules, ThreadStateIsSharedWithAggregate) {
    EXPECT_EQ(std_thread_module_current_id(),
              rstd::thread::current_id().as_u64().get().to_primitive());
}

TEST(StdModules, DirectCondvarImport) {
    EXPECT_TRUE(std_condvar_module());
}
