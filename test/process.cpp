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
    auto child = res.unwrap();
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
    auto out = res.unwrap();
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
    auto out = res.unwrap();

    auto* p = reinterpret_cast<const char*>(out.stderr_buf.begin());
    EXPECT_EQ(std::string(p, out.stderr_buf.len()), "err\n");
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
        auto msg = reinterpret_cast<const rstd::u8*>("hello pipe");
        auto wres = rstd::as<rstd::io::Write>(stdin_h).write(msg, 10);
        ASSERT_TRUE(wres.is_ok());
    } // stdin_h dropped here, child sees EOF

    // Read from child's stdout via io::Read
    auto stdout_opt = child.take_stdout();
    ASSERT_TRUE(stdout_opt.is_some());
    {
        auto stdout_h = stdout_opt.unwrap();
        rstd::u8 buf[64] = {};
        auto rres = rstd::as<rstd::io::Read>(stdout_h).read(buf, sizeof(buf));
        ASSERT_TRUE(rres.is_ok());
        EXPECT_EQ(rres.unwrap(), 10u);
        EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), 10), "hello pipe");
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
    auto* p = reinterpret_cast<const char*>(out.stdout_buf.begin());
    EXPECT_EQ(std::string(p, out.stdout_buf.len()), "collected\n");
}

TEST(Process, StdioNull) {
    auto res = rstd::process::Command::make("echo")
                   .arg("silenced")
                   .set_stdout(rstd::process::Stdio::null())
                   .status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_TRUE(res.unwrap().success());
}
