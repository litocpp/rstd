import rstd;
#include <gtest/gtest.h>

using namespace rstd::prelude;

TEST(Process, ExitStatusSuccess) {
    auto s = rstd::process::ExitStatus::from_code(0);
    EXPECT_TRUE(s.success());
    EXPECT_TRUE(s.code().is_some());
    EXPECT_EQ(s.code().unwrap(), 0);
}

TEST(Process, ExitStatusFailure) {
    auto s = rstd::process::ExitStatus::from_code(1);
    EXPECT_FALSE(s.success());
    EXPECT_EQ(s.code().unwrap(), 1);
}

TEST(Process, CommandStatusTrue) {
    auto res = rstd::process::Command::make("true").status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_TRUE(res.unwrap().success());
}

TEST(Process, CommandStatusFalse) {
    auto res = rstd::process::Command::make("false").status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_FALSE(res.unwrap().success());
    EXPECT_EQ(res.unwrap().code().unwrap(), 1);
}

TEST(Process, CommandWithArgs) {
    auto res = rstd::process::Command::make("echo")
                   .arg("hello")
                   .arg("world")
                   .set_stdout(rstd::process::Stdio::piped())
                   .spawn();
    ASSERT_TRUE(res.is_ok());
    auto child = rstd::move(res.unwrap());
    EXPECT_GT(child.id(), 0u);

    auto status = child.wait();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status.unwrap().success());
}

TEST(Process, CommandOutput) {
    auto res = rstd::process::Command::make("echo")
                   .arg("hello")
                   .output();
    ASSERT_TRUE(res.is_ok());
    auto out = rstd::move(res.unwrap());
    EXPECT_TRUE(out.status.success());

    // stdout should contain "hello\n"
    EXPECT_GE(out.stdout_buf.len(), 5u);
    auto* p = reinterpret_cast<const char*>(out.stdout_buf.begin());
    EXPECT_EQ(std::string(p, out.stdout_buf.len()), "hello\n");
}

TEST(Process, CommandOutputStderr) {
    // sh -c 'echo err >&2'
    auto res = rstd::process::Command::make("sh")
                   .arg("-c")
                   .arg("echo err >&2")
                   .output();
    ASSERT_TRUE(res.is_ok());
    auto out = rstd::move(res.unwrap());

    auto* p = reinterpret_cast<const char*>(out.stderr_buf.begin());
    EXPECT_EQ(std::string(p, out.stderr_buf.len()), "err\n");
}

TEST(Process, CommandNotFound) {
    auto res = rstd::process::Command::make("nonexistent_program_xyz_12345").status();
    EXPECT_TRUE(res.is_err());
}

TEST(Process, StdioNull) {
    auto res = rstd::process::Command::make("echo")
                   .arg("silenced")
                   .set_stdout(rstd::process::Stdio::null())
                   .status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_TRUE(res.unwrap().success());
}
