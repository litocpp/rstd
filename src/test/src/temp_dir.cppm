export module rstd.test:temp_dir;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::test
{

rstd::sync::atomic::Atomic<usize> temp_directory_sequence;

auto report_temp_directory_cleanup_failure(ref<rstd::path::Path>         path,
                                           const rstd::io::error::Error& error) noexcept -> void {
    auto message = rstd::format("cannot remove temporary directory '{}': {}", path, error);
    if (current_test_context() != nullptr) {
        fail_current(message.as_str(), __FILE__, __LINE__, false);
        return;
    }
    rstd::io::eprintln("rstd.test: {}", message.as_str());
}

} // namespace rstd::test

export namespace rstd::test
{

class TempDir {
    Option<rstd::path::PathBuf> path_;

    explicit TempDir(rstd::path::PathBuf path): path_(Some(rstd::move(path))) {}

    auto close_best_effort() noexcept -> void {
        if (path_.is_none()) return;
        auto result = rstd::fs::remove_dir_all(path_->as_path());
        if (result.is_ok()) {
            path_ = None();
            return;
        }
        report_temp_directory_cleanup_failure(path_->as_path(), result.unwrap_err());
    }

public:
    TempDir() noexcept                         = default;
    TempDir(const TempDir&)                    = delete;
    auto operator=(const TempDir&) -> TempDir& = delete;

    TempDir(TempDir&& other) noexcept: path_(rstd::move(other.path_)) { other.path_ = None(); }

    auto operator=(TempDir&& other) noexcept -> TempDir& {
        if (this == rstd::addressof(other)) return *this;
        close_best_effort();
        path_       = rstd::move(other.path_);
        other.path_ = None();
        return *this;
    }

    ~TempDir() noexcept { close_best_effort(); }

    static auto make() -> rstd::io::Result<TempDir> {
        constexpr auto attempt_limit = usize(1024);
        auto           base          = rstd::env::temp_dir();
        for (auto attempt = usize {}; attempt < attempt_limit; ++attempt) {
            auto sequence =
                temp_directory_sequence.fetch_add(usize(1), rstd::sync::atomic::Ordering::Relaxed);
            auto name   = rstd::format("rstd-test-{}-{}", rstd::process::id(), sequence);
            auto path   = base.join(rstd::path::PathBuf::from(name.as_str()).as_path());
            auto result = rstd::fs::create_dir(path.as_path());
            if (result.is_ok()) return Ok(TempDir(rstd::move(path)));
            auto error = rstd::move(result).unwrap_err();
            if (error.kind() !=
                rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::AlreadyExists }) {
                return Err(rstd::move(error));
            }
        }
        return Err(rstd::io::error::Error::new_const(
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::AlreadyExists },
            "cannot create a unique test temporary directory"));
    }

    auto path() const -> ref<rstd::path::Path> { return path_->as_path(); }

    auto close() -> rstd::io::Result<empty> {
        if (path_.is_none()) return Ok(empty {});
        auto result = rstd::fs::remove_dir_all(path_->as_path());
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        path_ = None();
        return Ok(empty {});
    }

    auto keep() -> rstd::path::PathBuf {
        auto path = rstd::move(path_).unwrap();
        path_     = None();
        return path;
    }
};

} // namespace rstd::test
