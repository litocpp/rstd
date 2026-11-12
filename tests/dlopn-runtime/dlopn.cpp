#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>
import rstd.dlopn;

#if ! RSTD_OS_WINDOWS
TEST(Dlopn, ExportsRawApi) {
    auto* handle =
        rstd::dlopn::dlopen("libm.so.6", rstd::dlopn::rtld_now | rstd::dlopn::rtld_local);
    ASSERT_NE(handle, nullptr);
    EXPECT_NE(rstd::dlopn::dlsym(handle, "cos"), nullptr);
    EXPECT_EQ(rstd::dlopn::dlclose(handle), 0);
}

TEST(Dlopn, LoadsTypedSymbol) {
    auto library = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libm.so.6"));
    ASSERT_TRUE(library.is_ok());
    auto loaded = rstd::move(library).unwrap_unchecked();

    auto symbol = loaded.symbol<double (*)(double)>(rstd::ffi::CStr::from_ptr("cos"));
    ASSERT_TRUE(symbol.is_ok());
    EXPECT_DOUBLE_EQ(symbol.unwrap_unchecked()(0.0), 1.0);
}
#else
using namespace rstd::prelude;
using namespace rstd::literals;

TEST(Dlopn, WindowsLoadsTypedSymbolAndMoves) {
    auto library = rstd::dlopn::WindowsLibrary::open(rstd::ffi::CStr::from_ptr("kernel32.dll"));
    ASSERT_TRUE(library.is_ok());
    auto loaded = rstd::move(library).unwrap_unchecked();
    auto other  = rstd::dlopn::WindowsLibrary::open(rstd::ffi::CStr::from_ptr("kernel32.dll"));
    ASSERT_TRUE(other.is_ok());
    loaded             = rstd::move(other).unwrap_unchecked();
    using GetProcessId = unsigned long(__stdcall*)();
    auto symbol = loaded.symbol<GetProcessId>(rstd::ffi::CStr::from_ptr("GetCurrentProcessId"));
    ASSERT_TRUE(symbol.is_ok());
    EXPECT_GT(symbol.unwrap_unchecked()(), 0u);
    auto missing =
        loaded.symbol<GetProcessId>(rstd::ffi::CStr::from_ptr("vui_missing_symbol_7aeee160"));
    ASSERT_TRUE(missing.is_err());
    auto error = rstd::move(missing).unwrap_err_unchecked();
    EXPECT_TRUE(error.raw_os_error().is_some());
    EXPECT_FALSE(error.message().is_empty());
}

TEST(Dlopn, WindowsNativePathsAndErrors) {
    auto invalid = rstd::dlopn::WindowsLibrary::open(rstd::ffi::CStr::from_ptr("\xff"));
    ASSERT_TRUE(invalid.is_err());
    EXPECT_TRUE(invalid.unwrap_err().raw_os_error().is_none());
    auto missing = rstd::dlopn::WindowsLibrary::open(
        rstd::ffi::CStr::from_ptr("rstd_missing_library_7aeee160.dll"));
    ASSERT_TRUE(missing.is_err());
    EXPECT_TRUE(missing.unwrap_err().raw_os_error().is_some());
    auto name = rstd::ffi::OsString::from("kernel32.dll"_str);
    EXPECT_TRUE(rstd::dlopn::WindowsLibrary::open_native(name.as_os_str()).is_ok());
    constexpr rstd::uint32_t load_library_search_system32 = 0x800;
    EXPECT_TRUE(
        rstd::dlopn::WindowsLibrary::open_native(name.as_os_str(), load_library_search_system32)
            .is_ok());
    u16  units[] = { u16('a'), u16(0), u16('b') };
    auto with_nul =
        rstd::os::windows::ffi::OsStringExt::from_wide(slice<u16>::from_raw_parts(units, usize(3)));
    auto rejected = rstd::dlopn::WindowsLibrary::open_native(with_nul.as_os_str());
    ASSERT_TRUE(rejected.is_err());
    EXPECT_TRUE(rejected.unwrap_err().raw_os_error().is_none());
}
#endif
