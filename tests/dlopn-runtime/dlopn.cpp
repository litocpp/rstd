#include <rstd/test/gtest.hpp>
import rstd.dlopn;

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
