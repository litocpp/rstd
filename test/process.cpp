#include <gtest/gtest.h>
#include <string>

import rstd;

using namespace rstd::prelude;

namespace
{

auto to_std_string(const rstd::vec::Vec<rstd::u8>& bytes) -> std::string {
    auto result = std::string {};
    result.reserve(bytes.len().to_primitive());
    for (auto index = rstd::usize(); index < bytes.len(); ++index) {
        result.push_back(static_cast<char>(bytes[index].to_primitive()));
    }
    return result;
}

} // namespace

TEST(Process, ExitStatusSuccess) {
    auto s = rstd::process::ExitStatus::from_code(rstd::i32());
    EXPECT_TRUE(s.success());
    EXPECT_TRUE(s.code().is_some());
    EXPECT_EQ(s.code().unwrap(), rstd::i32());
}

TEST(Process, ExitStatusFailure) {
    auto s = rstd::process::ExitStatus::from_code(rstd::i32(1));
    EXPECT_FALSE(s.success());
    EXPECT_EQ(s.code().unwrap(), rstd::i32(1));
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
    EXPECT_EQ(res.unwrap().code().unwrap(), rstd::i32(1));
}

TEST(Process, CommandWithArgs) {
    auto res = rstd::process::Command::make("echo")
                   .arg("hello")
                   .arg("world")
                   .set_stdout(rstd::process::Stdio::piped())
                   .spawn();
    ASSERT_TRUE(res.is_ok());
    auto child = res.unwrap();
    EXPECT_GT(child.id(), rstd::u32());

    auto status = child.wait();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status.unwrap().success());
}

TEST(Process, CommandOutput) {
    auto res = rstd::process::Command::make("echo").arg("hello").output();
    ASSERT_TRUE(res.is_ok());
    auto out = res.unwrap();
    EXPECT_TRUE(out.status.success());

    // stdout should contain "hello\n"
    EXPECT_GE(out.stdout_buf.len(), rstd::usize(5));
    EXPECT_EQ(to_std_string(out.stdout_buf), "hello\n");
}

TEST(Process, CommandOutputStderr) {
    // sh -c 'echo err >&2'
    auto res = rstd::process::Command::make("sh").arg("-c").arg("echo err >&2").output();
    ASSERT_TRUE(res.is_ok());
    auto out = res.unwrap();

    EXPECT_EQ(to_std_string(out.stderr_buf), "err\n");
}

TEST(Process, CommandNotFound) {
    auto res = rstd::process::Command::make("nonexistent_program_xyz_12345").status();
    EXPECT_TRUE(res.is_err());
}

TEST(Process, ChildStdinWrite) {
    // cat reads from stdin and writes to stdout
    auto res = rstd::process::Command::make("cat")
                   .set_stdin(rstd::process::Stdio::piped())
                   .set_stdout(rstd::process::Stdio::piped())
                   .spawn();
    ASSERT_TRUE(res.is_ok());
    auto child = res.unwrap();

    // Write to child's stdin via io::Write
    auto stdin_opt = child.take_stdin();
    ASSERT_TRUE(stdin_opt.is_some());
    {
        auto stdin_h = stdin_opt.unwrap();
        auto raw     = rstd::slice<rstd::byte>::from_raw_parts(
            reinterpret_cast<rstd::byte const*>("hello pipe"), rstd::usize(10));
        auto wres = rstd::as<rstd::io::Write>(stdin_h).write(raw);
        ASSERT_TRUE(wres.is_ok());
    } // stdin_h dropped here, child sees EOF

    // Read from child's stdout via io::Read
    auto stdout_opt = child.take_stdout();
    ASSERT_TRUE(stdout_opt.is_some());
    {
        auto     stdout_h = stdout_opt.unwrap();
        rstd::u8 buf[64]  = {};
        auto     values = rstd::mut_ref<rstd::u8[]>::from_raw_parts(buf, rstd::usize(sizeof(buf)));
        auto     rres   = rstd::as<rstd::io::Read>(stdout_h).read(rstd::as_bytes_mut(values));
        ASSERT_TRUE(rres.is_ok());
        EXPECT_EQ(rres.unwrap(), rstd::usize(10));
        auto bytes = rstd::vec::Vec<rstd::u8>::make();
        for (auto index = rstd::usize(); index < rstd::usize(10); ++index) {
            bytes.emplace_back(buf[index.to_primitive()]);
        }
        EXPECT_EQ(to_std_string(bytes), "hello pipe");
    }

    auto status = child.wait();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status.unwrap().success());
}

TEST(Process, WaitWithOutput) {
    auto res = rstd::process::Command::make("echo")
                   .arg("collected")
                   .set_stdout(rstd::process::Stdio::piped())
                   .spawn();
    ASSERT_TRUE(res.is_ok());
    auto out_res = res.unwrap().wait_with_output();
    ASSERT_TRUE(out_res.is_ok());
    auto out = out_res.unwrap();
    EXPECT_TRUE(out.status.success());
    EXPECT_EQ(to_std_string(out.stdout_buf), "collected\n");
}

TEST(Process, StdioNull) {
    auto res = rstd::process::Command::make("echo")
                   .arg("silenced")
                   .set_stdout(rstd::process::Stdio::null())
                   .status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_TRUE(res.unwrap().success());
}
