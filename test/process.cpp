#include <rstd/test/gtest.hpp>
#include <string>

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

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
    auto res = rstd::process::Command::make("true"_str).status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_TRUE(res.unwrap().success());
}

TEST(Process, CommandStatusFalse) {
    auto res = rstd::process::Command::make("false"_str).status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_FALSE(res.unwrap().success());
    EXPECT_EQ(res.unwrap().code().unwrap(), rstd::i32(1));
}

TEST(Process, CommandWithArgs) {
    auto res = rstd::process::Command::make("echo"_str)
                   .arg("hello"_str)
                   .arg("world"_str)
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
    auto res = rstd::process::Command::make("echo"_str).arg("hello"_str).output();
    ASSERT_TRUE(res.is_ok());
    auto out = res.unwrap();
    EXPECT_TRUE(out.status.success());

    // stdout should contain "hello\n"
    EXPECT_GE(out.stdout_buf.len(), rstd::usize(5));
    EXPECT_EQ(to_std_string(out.stdout_buf), "hello\n");
}

TEST(Process, CommandOutputStderr) {
    // sh -c 'echo err >&2'
    auto res =
        rstd::process::Command::make("sh"_str).arg("-c"_str).arg("echo err >&2"_str).output();
    ASSERT_TRUE(res.is_ok());
    auto out = res.unwrap();

    EXPECT_EQ(to_std_string(out.stderr_buf), "err\n");
}

TEST(Process, CommandEnvironmentInheritsByDefault) {
    auto result = rstd::process::Command::make("/bin/sh"_str)
                      .arg("-c"_str)
                      .arg("test -n \"${PATH+x}\""_str)
                      .status();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->success());
}

TEST(Process, CommandEnvironmentUsesLastOverride) {
    auto result = rstd::process::Command::make("/bin/sh"_str)
                      .arg("-c"_str)
                      .arg("test \"$RSTD_PROCESS_ENV_OVERRIDE\" = second"_str)
                      .env("RSTD_PROCESS_ENV_OVERRIDE"_str, "first"_str)
                      .env("RSTD_PROCESS_ENV_OVERRIDE"_str, "second"_str)
                      .status();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->success());
}

TEST(Process, CommandEnvironmentCanRemoveInheritedValue) {
    auto result = rstd::process::Command::make("/usr/bin/env"_str).env_remove("PATH"_str).output();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->status.success());
    auto environment = to_std_string(result->stdout_buf);
    EXPECT_NE(environment.rfind("PATH=", 0), std::size_t {});
    EXPECT_EQ(environment.find("\nPATH="), std::string::npos);
}

TEST(Process, CommandEnvironmentCanClearAndAddValues) {
    auto result = rstd::process::Command::make("/usr/bin/env"_str)
                      .env_clear()
                      .env("RSTD_PROCESS_ENV_ONLY"_str, "value"_str)
                      .output();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->status.success());
    EXPECT_EQ(to_std_string(result->stdout_buf), "RSTD_PROCESS_ENV_ONLY=value\n");
}

TEST(Process, CommandEnvironmentDoesNotMutateParent) {
    constexpr auto key = "RSTD_PROCESS_ENV_PARENT_GUARD"_str;
    ASSERT_TRUE(rstd::env::var(key).is_none());
    auto result = rstd::process::Command::make("/bin/true"_str).env(key, "child"_str).status();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->success());
    EXPECT_TRUE(rstd::env::var(key).is_none());
}

TEST(Process, CommandEnvironmentRejectsNul) {
    auto bytes = rstd::vec::Vec<rstd::u8>::make();
    bytes.push(rstd::u8('x'));
    bytes.push(rstd::u8());
    bytes.push(rstd::u8('y'));
    auto value  = rstd::ffi::OsString::from_encoded_bytes_unchecked(rstd::move(bytes));
    auto result = rstd::process::Command::make("/bin/true"_str)
                      .env("RSTD_PROCESS_ENV_NUL"_str, value.as_os_str())
                      .status();
    EXPECT_TRUE(result.is_err());
}

TEST(Process, CommandCurrentDirectory) {
    auto directory = rstd::path::PathBuf::from("/tmp"_str);
    auto res = rstd::process::Command::make("pwd"_str).current_dir(directory.as_path()).output();
    ASSERT_TRUE(res.is_ok());
    auto out = res.unwrap();
    EXPECT_TRUE(out.status.success());
    EXPECT_EQ(to_std_string(out.stdout_buf), "/tmp\n");
}

TEST(Process, CommandCurrentDirectoryReportsSpawnFailure) {
    auto directory = rstd::path::PathBuf::from("/rstd-directory-that-does-not-exist"_str);
    auto result    = rstd::process::Command::make("pwd"_str)
                         .current_dir(directory.as_path())
                         .env("PATH"_str, "/usr/bin"_str)
                         .status();
    EXPECT_TRUE(result.is_err());
}

TEST(Process, CommandCurrentDirectoryWorksWithForkExecFallback) {
    auto directory = rstd::path::PathBuf::from("/tmp"_str);
    auto result    = rstd::process::Command::make("pwd"_str)
                         .current_dir(directory.as_path())
                         .env("PATH"_str, "/usr/bin"_str)
                         .output();
    ASSERT_TRUE(result.is_ok());
    auto output = result.unwrap();
    EXPECT_TRUE(output.status.success());
    EXPECT_EQ(to_std_string(output.stdout_buf), "/tmp\n");
}

TEST(Process, ChildTryWait) {
    auto child =
        rstd::process::Command::make("sh"_str).arg("-c"_str).arg("sleep 0.02; exit 7"_str).spawn();
    ASSERT_TRUE(child.is_ok());
    auto running = rstd::move(child).unwrap();
    auto first   = running.try_wait();
    ASSERT_TRUE(first.is_ok());

    auto status = rstd::Option<rstd::process::ExitStatus> {};
    for (int attempt = 0; attempt < 100 && status.is_none(); ++attempt) {
        rstd::thread::sleep(rstd::time::Duration::from_millis(rstd::u64(1)));
        auto waited = running.try_wait();
        ASSERT_TRUE(waited.is_ok());
        status = rstd::move(waited).unwrap();
    }
    ASSERT_TRUE(status.is_some());
    ASSERT_TRUE(status->code().is_some());
    EXPECT_EQ(*status->code(), rstd::i32(7));
    auto waited = running.wait();
    ASSERT_TRUE(waited.is_ok());
    EXPECT_EQ(*waited->code(), rstd::i32(7));
}

TEST(Process, CommandNotFound) {
    auto res = rstd::process::Command::make("nonexistent_program_xyz_12345"_str).status();
    EXPECT_TRUE(res.is_err());
}

TEST(Process, ChildStdinWrite) {
    // cat reads from stdin and writes to stdout
    auto res = rstd::process::Command::make("cat"_str)
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
        auto wres    = rstd::as<rstd::io::Write>(stdin_h).write("hello pipe"_bytes);
        ASSERT_TRUE(wres.is_ok());
    } // stdin_h dropped here, child sees EOF

    // Read from child's stdout via io::Read
    auto stdout_opt = child.take_stdout();
    ASSERT_TRUE(stdout_opt.is_some());
    {
        auto stdout_h = stdout_opt.unwrap();
        auto buf      = rstd::array<rstd::u8, 64> {};
        auto rres     = rstd::as<rstd::io::Read>(stdout_h).read(buf.as_mut_slice());
        ASSERT_TRUE(rres.is_ok());
        EXPECT_EQ(rres.unwrap(), rstd::usize(10));
        auto bytes = rstd::vec::Vec<rstd::u8>::make();
        for (auto index = rstd::usize(); index < rstd::usize(10); ++index) {
            bytes.emplace_back(buf[index]);
        }
        EXPECT_EQ(to_std_string(bytes), "hello pipe");
    }

    auto status = child.wait();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status.unwrap().success());
}

TEST(Process, WaitWithOutput) {
    auto res = rstd::process::Command::make("echo"_str)
                   .arg("collected"_str)
                   .set_stdout(rstd::process::Stdio::piped())
                   .spawn();
    ASSERT_TRUE(res.is_ok());
    auto out_res = res.unwrap().wait_with_output();
    ASSERT_TRUE(out_res.is_ok());
    auto out = out_res.unwrap();
    EXPECT_TRUE(out.status.success());
    EXPECT_EQ(to_std_string(out.stdout_buf), "collected\n");
}

TEST(Process, WaitWithLargeOutputOnBothPipes) {
    auto res = rstd::process::Command::make("sh"_str)
                   .arg("-c"_str)
                   .arg("head -c 131072 /dev/zero; head -c 131072 /dev/zero >&2"_str)
                   .output();
    ASSERT_TRUE(res.is_ok());
    auto out = res.unwrap();
    EXPECT_TRUE(out.status.success());
    EXPECT_EQ(out.stdout_buf.len(), rstd::usize(131072));
    EXPECT_EQ(out.stderr_buf.len(), rstd::usize(131072));
}

TEST(Process, StdioNull) {
    auto res = rstd::process::Command::make("echo"_str)
                   .arg("silenced"_str)
                   .set_stdout(rstd::process::Stdio::null())
                   .status();
    ASSERT_TRUE(res.is_ok());
    EXPECT_TRUE(res.unwrap().success());
}
