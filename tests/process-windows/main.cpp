#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>
#include <windows.h>

import rstd;
import rstd.test;
import rstd.os.handle;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd;
using process::Command;
using process::Stdio;

auto self_command(ref<str> mode) -> Command {
    wchar_t path[32768];
    auto    length = ::GetModuleFileNameW(nullptr, path, 32768);
    rstd_assert(length > 0 && length < 32768);
    Vec<u16> units;
    for (DWORD index = 0; index < length; ++index) units.push(u16(path[index]));
    auto name    = os::windows::ffi::OsStringExt::from_wide(units.as_slice());
    auto command = Command::make(name.as_os_str());
    command.arg("--child"_str).arg(mode);
    return command;
}

auto run(async::coro<bool> operation) -> bool {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result  = runtime.block_on(
        async::timeout(rstd::move(operation).into_future(), time::Duration::from_secs(u64(10))));
    return result.is_ok() && *result;
}

auto read_all(async::pipe::Receiver reader) -> async::coro<io::Result<Vec<u8>>> {
    Vec<u8> result;
    for (;;) {
        auto chunk = co_await reader.read(usize(113));
        if (chunk.is_err()) co_return Err(rstd::move(chunk).unwrap_err());
        if (chunk->is_empty()) co_return Ok(rstd::move(result));
        result.extend_from_slice(chunk->as_slice());
    }
}

auto capture_and_wait() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("capture"_str);
    command.arg("argument"_str)
        .env("RSTD_ASYNC_VALUE"_str, "value"_str)
        .set_stdout(Stdio::piped())
        .set_stderr(Stdio::piped());
    auto child   = supervisor.handle().spawn(command).unwrap();
    auto out     = co_await read_all(child.take_stdout().unwrap());
    auto err     = co_await read_all(child.take_stderr().unwrap());
    auto status  = co_await child.wait();
    auto again   = co_await child.wait();
    auto stopped = co_await supervisor.shutdown();
    co_return out.is_ok() && out->as_slice() == "value:argument"_bytes && err.is_ok() &&
        err->as_slice() == "error"_bytes && status.is_ok() && status->code() == Some(i32(17)) &&
        again.is_ok() && again->code() == Some(i32(17)) && stopped.is_ok();
}

auto exit_259() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("259"_str);
    auto child      = supervisor.handle().spawn(command).unwrap();
    auto first      = async::spawn(child.wait());
    auto second     = co_await child.wait();
    auto waited     = co_await first;
    auto cached     = child.try_wait();
    co_return second.is_ok() && second->code() == Some(i32(259)) && waited.is_ok() &&
        waited->is_ok() && (*waited)->code() == Some(i32(259)) && cached.is_ok() &&
        cached->is_some() && (**cached).code() == Some(i32(259)) &&
        (co_await supervisor.shutdown()).is_ok();
}

auto input_roundtrip(bool take_input) -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("echo"_str);
    command.set_stdin(Stdio::piped()).set_stdout(take_input ? Stdio::piped() : Stdio::null());
    auto spawned = supervisor.handle().spawn(command);
    if (spawned.is_err()) {
        io::eprintln { "stdin pipe spawn: {}", spawned.unwrap_err() };
        co_return false;
    }
    auto child = rstd::move(spawned).unwrap();
    if (take_input) {
        auto input    = child.take_stdin().unwrap();
        auto output   = async::spawn(read_all(child.take_stdout().unwrap()));
        auto canceled = co_await async::timeout(child.wait().into_future(),
                                                time::Duration::from_millis(u64(5)));
        if (canceled.is_ok()) co_return false;
        Vec<u8> data;
        data.resize(usize(262144), u8(42));
        auto written = co_await input.write_all(bytes::Bytes::from_vec(rstd::move(data)));
        if (written.is_err() || *written != usize(262144)) co_return false;
        if ((co_await rstd::move(input).close()).is_err()) co_return false;
        auto received = co_await output;
        if (received.is_err() || received->is_err() || (*received)->len() != usize(262144))
            co_return false;
        for (auto value : (*received)->as_slice())
            if (value != u8(42)) co_return false;
    }
    auto status = co_await child.wait();
    co_return status.is_ok() && status->success() && (co_await supervisor.shutdown()).is_ok();
}

auto canceled_read_preserves_data() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("delayed"_str);
    command.set_stdout(Stdio::piped());
    auto child    = supervisor.handle().spawn(command).unwrap();
    auto output   = child.take_stdout().unwrap();
    auto canceled = co_await async::timeout(output.read(usize(64)).into_future(),
                                            time::Duration::from_millis(u64(5)));
    if (canceled.is_ok()) co_return false;
    Vec<u8> received;
    for (;;) {
        auto chunk = co_await output.read(usize(2));
        if (chunk.is_err() || chunk->len() > usize(2)) co_return false;
        if (chunk->is_empty()) break;
        received.extend_from_slice(chunk->as_slice());
    }
    auto status = co_await child.wait();
    co_return received.as_slice() == "abcdef"_bytes && status.is_ok() && status->success() &&
        (co_await rstd::move(output).close()).is_ok() && (co_await supervisor.shutdown()).is_ok();
}

auto process_failures_and_kill() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto missing    = Command::make("C:\\rstd-no-such-program-20261103.exe"_str);
    if (supervisor.handle().spawn(missing).is_ok()) co_return false;
    auto command = self_command("sleep"_str);
    auto child   = supervisor.handle().spawn(command).unwrap();
    auto before  = child.try_wait();
    if (before.is_err() || before->is_some()) co_return false;
    if ((co_await child.kill()).is_err()) co_return false;
    auto status = co_await child.wait();
    if (status.is_err() || status->success()) co_return false;
    supervisor.stop_accepting();
    auto rejected = supervisor.handle().spawn(command);
    co_return rejected.is_err() &&
        rejected.unwrap_err().kind() == io::ErrorKind { io::ErrorKind::NotConnected } &&
        (co_await supervisor.shutdown()).is_ok();
}

auto dropped_child_shutdown() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("sleep"_str);
    {
        auto child = supervisor.handle().spawn(command).unwrap();
    }
    co_return (co_await supervisor.shutdown()).is_ok();
}

auto close_pending_read() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("sleep"_str);
    command.set_stdout(Stdio::piped());
    auto child  = supervisor.handle().spawn(command).unwrap();
    auto output = child.take_stdout().unwrap();
    auto pending =
        co_await async::timeout(output.read().into_future(), time::Duration::from_millis(u64(5)));
    if (pending.is_ok()) co_return false;
    auto closed = co_await rstd::move(output).close();
    co_return closed.is_ok() && (co_await child.kill()).is_ok() &&
        (co_await supervisor.shutdown()).is_ok();
}

struct RuntimeTeardownState {
    async::ProcessSupervisor supervisor;
    async::Child             child;
    async::pipe::Receiver    output;
    os::handle::OwnedHandle  process;
    bool                     canceled;
};

auto prepare_runtime_teardown() -> async::coro<RuntimeTeardownState> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto command    = self_command("sleep"_str);
    command.set_stdout(Stdio::piped());
    auto child   = supervisor.handle().spawn(command).unwrap();
    auto process = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE,
                                 static_cast<DWORD>(child.id().to_primitive()));
    rstd_assert(process != nullptr);
    auto output = child.take_stdout().unwrap();
    auto pending =
        co_await async::timeout(output.read().into_future(), time::Duration::from_millis(u64(5)));
    co_return RuntimeTeardownState { rstd::move(supervisor),
                                     rstd::move(child),
                                     rstd::move(output),
                                     os::handle::OwnedHandle::from_raw_handle(process),
                                     pending.is_err() };
}

TEST(WindowsAsyncProcess, RuntimeDestructionDrainsReadAndTerminatesOwnedChild) {
    Option<RuntimeTeardownState> retained;
    {
        auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
        retained.insert(runtime.block_on(prepare_runtime_teardown()));
        EXPECT_TRUE(retained->canceled);
        EXPECT_EQ(::WaitForSingleObject(retained->process.as_raw_handle(), 0), DWORD(WAIT_TIMEOUT));
    }
    EXPECT_EQ(::WaitForSingleObject(retained->process.as_raw_handle(), 5000), DWORD(WAIT_OBJECT_0));
    DWORD code;
    ASSERT_TRUE(::GetExitCodeProcess(retained->process.as_raw_handle(), &code));
    EXPECT_NE(code, DWORD(STILL_ACTIVE));
    retained = None();
}

TEST(WindowsAsyncProcess, CapturesConfiguredOutputAndCachesWait) {
    EXPECT_TRUE(run(capture_and_wait()));
}
TEST(WindowsAsyncProcess, Exit259CompletesAllWaiters) {
    EXPECT_TRUE(run(exit_259()));
}
TEST(WindowsAsyncProcess, UntakenStdinClosesOnWait) {
    EXPECT_TRUE(run(input_roundtrip(false)));
}
TEST(WindowsAsyncProcess, CanceledWaitRetainsOwnerAndPipesHandleBackpressure) {
    EXPECT_TRUE(run(input_roundtrip(true)));
}
TEST(WindowsAsyncProcess, FailedSpawnReleasesReservationAndKillWaits) {
    EXPECT_TRUE(run(process_failures_and_kill()));
}
TEST(WindowsAsyncProcess, DroppedChildCanBeDrainedByShutdown) {
    EXPECT_TRUE(run(dropped_child_shutdown()));
}
TEST(WindowsAsyncPipe, CanceledReadPreservesBytesAndRespectsNextLimit) {
    EXPECT_TRUE(run(canceled_read_preserves_data()));
}
TEST(WindowsAsyncPipe, CloseDrainsCanceledPendingRead) {
    EXPECT_TRUE(run(close_pending_read()));
}

TEST(WindowsProcess, TryWaitRecognizesExited259) {
    auto                        command = self_command("259"_str);
    auto                        child   = command.spawn().unwrap();
    Option<process::ExitStatus> status;
    for (int attempt = 0; attempt < 500 && status.is_none(); ++attempt) {
        status = child.try_wait().unwrap();
        if (status.is_none()) ::Sleep(1);
    }
    ASSERT_TRUE(status.is_some());
    EXPECT_EQ(status->code(), Some(i32(259)));
    EXPECT_EQ(child.wait().unwrap().code(), Some(i32(259)));
}

TEST(WindowsProcess, SynchronousOutputPreservesArgumentsAndEnvironment) {
    auto command = self_command("capture"_str);
    command.arg("argument"_str).env("RSTD_ASYNC_VALUE"_str, "value"_str);
    auto output = command.output();
    ASSERT_TRUE(output.is_ok());
    EXPECT_EQ(output->status.code(), Some(i32(17)));
    EXPECT_EQ(output->stdout_buf.as_slice(), "value:argument"_bytes);
    EXPECT_EQ(output->stderr_buf.as_slice(), "error"_bytes);
}

TEST(WindowsProcess, SynchronousStdioReadsWritesAndClosesAtEof) {
    auto command = self_command("echo"_str);
    command.set_stdin(Stdio::piped()).set_stdout(Stdio::piped());
    auto child = command.spawn().unwrap();
    {
        auto input   = child.take_stdin().unwrap();
        auto written = as<io::Write>(input).write("synchronous"_bytes);
        ASSERT_TRUE(written.is_ok());
        EXPECT_EQ(*written, usize(11));
    }
    auto          output = child.take_stdout().unwrap();
    Vec<u8>       received;
    array<u8, 32> buffer {};
    for (;;) {
        auto read = as<io::Read>(output).read(buffer.as_mut_slice());
        ASSERT_TRUE(read.is_ok());
        if (*read == usize()) break;
        received.extend_from_slice(
            slice<u8>::from_raw_parts(buffer.as_slice().as_raw_ptr(), *read));
    }
    EXPECT_EQ(received.as_slice(), "synchronous"_bytes);
    EXPECT_TRUE(child.wait().unwrap().success());
}

TEST(WindowsProcess, AsyncStdioRejectsSynchronousIo) {
    auto command = self_command("echo"_str);
    command.set_stdin(Stdio::piped()).set_stdout(Stdio::piped()).set_stderr(Stdio::piped());
    auto child = command.spawn_with_async_pipes().unwrap();
    {
        auto input   = child.take_stdin().unwrap();
        auto written = as<io::Write>(input).write("unsubmitted"_bytes);
        ASSERT_TRUE(written.is_err());
        EXPECT_EQ(written.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    }
    array<u8, 8> buffer {};
    auto         output     = child.take_stdout().unwrap();
    auto         error      = child.take_stderr().unwrap();
    auto         read       = as<io::Read>(output).read(buffer.as_mut_slice());
    auto         read_error = as<io::Read>(error).read(buffer.as_mut_slice());
    ASSERT_TRUE(read.is_err());
    ASSERT_TRUE(read_error.is_err());
    EXPECT_EQ(read.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    EXPECT_EQ(read_error.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    EXPECT_TRUE(child.wait().unwrap().success());
}

TEST(WindowsAsyncNotify, WakesWithoutIoDriverAndPreservesCanceledPermit) {
    auto notify  = async::Notify::make().unwrap();
    auto sender  = notify.notifier();
    auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
    auto thread  = rstd::thread::spawn([handle = sender.clone()] {
                      rstd::thread::sleep(time::Duration::from_millis(u64(5)));
                      return handle.notify();
                   }).unwrap();
    EXPECT_TRUE(runtime.block_on(notify.notified()).is_ok());
    EXPECT_TRUE(rstd::move(thread).join().unwrap().is_ok());
    auto cx       = task::Context { task::Waker::noop() };
    auto canceled = Some(notify.notified());
    auto next     = notify.notified();
    EXPECT_TRUE(future::poll(*canceled, cx).is_pending());
    EXPECT_TRUE(future::poll(next, cx).is_pending());
    EXPECT_TRUE(sender.notify().is_ok());
    canceled = None();
    EXPECT_TRUE(future::poll(next, cx).is_ready());
}

TEST(WindowsAsyncPipe, RejectsSynchronousPipesAndConsumesRejectedHandle) {
    HANDLE reader, writer;
    ASSERT_TRUE(::CreatePipe(&reader, &writer, nullptr, 0));
    auto accepted =
        async::pipe::Receiver::from_owned_handle(os::handle::OwnedHandle::from_raw_handle(reader));
    EXPECT_TRUE(accepted.is_err());
    DWORD flags;
    EXPECT_FALSE(::GetHandleInformation(reader, &flags));
    EXPECT_EQ(::GetLastError(), DWORD(ERROR_INVALID_HANDLE));
    ::CloseHandle(writer);
}

TEST(WindowsAsyncPipe, RejectsWrongDirectionOfOverlappedChildPipes) {
    auto command = self_command("echo"_str);
    command.set_stdin(Stdio::piped()).set_stdout(Stdio::piped());
    auto child  = command.spawn_with_async_pipes().unwrap();
    auto input  = child.take_stdin().unwrap();
    auto output = child.take_stdout().unwrap();
    auto wrong_reader =
        async::pipe::Receiver::from_owned_handle(rstd::move(input).into_owned_handle().unwrap());
    auto wrong_writer =
        async::pipe::Sender::from_owned_handle(rstd::move(output).into_owned_handle().unwrap());
    EXPECT_TRUE(wrong_reader.is_err());
    EXPECT_TRUE(wrong_writer.is_err());
    EXPECT_TRUE(child.wait().is_ok());
}

int main(int argc, char** argv) {
    if (argc > 2) {
        auto mode = ffi::CStr::from_ptr(argv[2]).to_str().unwrap();
        if (mode == "259"_str) return 259;
        if (mode == "sleep"_str) {
            ::Sleep(INFINITE);
            return 1;
        }
        if (mode == "capture"_str) {
            char value[64];
            auto count = ::GetEnvironmentVariableA("RSTD_ASYNC_VALUE", value, 64);
            if (count != 5 || argc != 4) return 2;
            DWORD written;
            if (! ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), value, count, &written, nullptr))
                return 3;
            if (! ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ":", 1, &written, nullptr))
                return 3;
            auto arg = ffi::CStr::from_ptr(argv[3]).to_str().unwrap();
            if (! ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),
                              arg.data(),
                              static_cast<DWORD>(arg.len().to_primitive()),
                              &written,
                              nullptr))
                return 3;
            if (! ::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), "error", 5, &written, nullptr))
                return 3;
            return 17;
        }
        if (mode == "delayed"_str) {
            ::Sleep(50);
            DWORD written;
            return ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), "abcdef", 6, &written, nullptr) &&
                           written == 6
                       ? 0
                       : 3;
        }
        if (mode == "echo"_str) {
            char buffer[4096];
            for (;;) {
                DWORD count;
                if (! ::ReadFile(
                        ::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &count, nullptr))
                    return ::GetLastError() == ERROR_BROKEN_PIPE ? 0 : 4;
                if (count == 0) return 0;
                DWORD offset = 0;
                while (offset < count) {
                    DWORD written;
                    if (! ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE),
                                      buffer + offset,
                                      count - offset,
                                      &written,
                                      nullptr) ||
                        written == 0)
                        return 5;
                    offset += written;
                }
            }
        }
        return 6;
    }
    return rstd::test::run_registered().to_primitive();
}
