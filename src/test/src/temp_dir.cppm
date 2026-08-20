export module rstd.test:temp_dir;

import rstd;
import :model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::test
{
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
    Option<rstd::fs::TempDir> directory_;

    explicit TempDir(rstd::fs::TempDir directory): directory_(Some(rstd::move(directory))) {}

    auto close_best_effort() noexcept -> void {
        if (directory_.is_none()) return;
        auto path   = rstd::path::PathBuf::from(directory_->path());
        auto result = directory_->close();
        if (result.is_ok()) {
            directory_ = None();
            return;
        }
        report_temp_directory_cleanup_failure(path.as_path(), result.unwrap_err());
    }

public:
    TempDir() noexcept                         = default;
    TempDir(const TempDir&)                    = delete;
    auto operator=(const TempDir&) -> TempDir& = delete;

    TempDir(TempDir&& other) noexcept: directory_(rstd::move(other.directory_)) {
        other.directory_ = None();
    }

    auto operator=(TempDir&& other) noexcept -> TempDir& {
        if (this == rstd::addressof(other)) return *this;
        close_best_effort();
        directory_       = rstd::move(other.directory_);
        other.directory_ = None();
        return *this;
    }

    ~TempDir() noexcept { close_best_effort(); }

    static auto make() -> rstd::io::Result<TempDir> {
        auto directory = rstd::fs::TempDir::make("rstd-test"_str);
        if (directory.is_err()) return Err(rstd::move(directory).unwrap_err());
        return Ok(TempDir(rstd::move(directory).unwrap()));
    }

    auto path() const -> ref<rstd::path::Path> { return directory_->path(); }

    auto close() -> rstd::io::Result<empty> {
        if (directory_.is_none()) return Ok(empty {});
        auto result = directory_->close();
        if (result.is_err()) return Err(rstd::move(result).unwrap_err());
        directory_ = None();
        return Ok(empty {});
    }

    auto keep() -> rstd::path::PathBuf {
        auto path  = directory_->keep();
        directory_ = None();
        return path;
    }
};

} // namespace rstd::test
