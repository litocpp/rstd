#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <rstd/test/gtest.hpp>

#include <string>

import rstd;
import rstd.test;

using namespace rstd::literals;

namespace
{

auto to_std_string(const rstd::vec::Vec<rstd::u8>& bytes) -> std::string {
    auto result = std::string {};
    result.reserve(bytes.len().to_primitive());
    for (auto index = rstd::usize {}; index < bytes.len(); ++index) {
        result.push_back(static_cast<char>(bytes[index].to_primitive()));
    }
    return result;
}

auto command(rstd::ref<rstd::str> script) -> rstd::process::Command {
    auto result = rstd::process::Command::make("cmd.exe"_str);
    result.arg("/d"_str).arg("/c"_str).arg(script);
    return result;
}

struct ObservedOutput {
    rstd::vec::Vec<rstd::u8> standard_output;
    rstd::vec::Vec<rstd::u8> standard_error;
};

void observe_output(void*                       raw_context,
                    rstd::process::OutputStream stream,
                    rstd::slice<rstd::u8>       bytes) noexcept {
    auto& output = *static_cast<ObservedOutput*>(raw_context);
    auto& target = stream == rstd::process::OutputStream::Stdout ? output.standard_output
                                                                 : output.standard_error;
    target.extend_from_slice(bytes);
}

} // namespace

TEST(ProcessWindows, CommandStatusPreservesExitCode) {
    auto success = command("exit /b 0"_str).status();
    ASSERT_TRUE(success.is_ok());
    EXPECT_TRUE(success->success());

    auto failure = command("exit /b 7"_str).status();
    ASSERT_TRUE(failure.is_ok());
    EXPECT_FALSE(failure->success());
    EXPECT_EQ(failure->code().unwrap(), rstd::i32(7));
}

TEST(ProcessWindows, CommandCollectsBothOutputStreams) {
    auto output = command("echo hello & echo error 1>&2"_str).output();
    ASSERT_TRUE(output.is_ok());
    EXPECT_TRUE(output->status.success());
    EXPECT_EQ(to_std_string(output->stdout_buf), "hello \r\n");
    EXPECT_EQ(to_std_string(output->stderr_buf), "error \r\n");
}

TEST(ProcessWindows, OutputObserverReceivesBothStreams) {
    auto observed = ObservedOutput {};
    auto output   = command("echo out & echo err 1>&2"_str)
                        .output(rstd::process::OutputObserver {
                            .context = &observed,
                            .notify  = observe_output,
                        });
    ASSERT_TRUE(output.is_ok());
    EXPECT_EQ(to_std_string(output->stdout_buf), to_std_string(observed.standard_output));
    EXPECT_EQ(to_std_string(output->stderr_buf), to_std_string(observed.standard_error));
}

TEST(ProcessWindows, EnvironmentUsesLastOverride) {
    auto process =
        command("if \"%RSTD_PROCESS_ENV_OVERRIDE%\"==\"second\" (exit /b 0) else (exit /b 1)"_str);
    auto status = process.env("RSTD_PROCESS_ENV_OVERRIDE"_str, "first"_str)
                      .env("RSTD_PROCESS_ENV_OVERRIDE"_str, "second"_str)
                      .status();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status->success());
}

TEST(ProcessWindows, CurrentDirectoryIsApplied) {
    auto temporary = rstd::test::TempDir::make().unwrap();
    auto status    = command("exit /b 0"_str).current_dir(temporary.path()).status();
    ASSERT_TRUE(status.is_ok());
    EXPECT_TRUE(status->success());
}

TEST(ProcessWindows, ChildTryWaitAndWaitAreConsistent) {
    auto child = command("ping -n 2 127.0.0.1 >nul & exit /b 9"_str).spawn();
    ASSERT_TRUE(child.is_ok());
    auto running = rstd::move(child).unwrap();
    auto status  = rstd::Option<rstd::process::ExitStatus> {};
    for (int attempt = 0; attempt < 200 && status.is_none(); ++attempt) {
        rstd::thread::sleep(rstd::time::Duration::from_millis(rstd::u64(10)));
        status = running.try_wait().unwrap();
    }
    ASSERT_TRUE(status.is_some());
    EXPECT_EQ(status->code().unwrap(), rstd::i32(9));
    EXPECT_EQ(running.wait()->code().unwrap(), rstd::i32(9));
}

TEST(ProcessWindows, CommandNotFound) {
    EXPECT_TRUE(
        rstd::process::Command::make("rstd-program-that-does-not-exist.exe"_str).status().is_err());
}
#endif
