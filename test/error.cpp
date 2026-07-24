#include <gtest/gtest.h>

import rstd;
import rstd.argparse;
import rstd.json;
import rstd.test.error_module_check;
import rstd.toml;

using namespace rstd;
using namespace rstd::literals;
using ::alloc::boxed::Box;
using ::alloc::vec::Vec;
using rstd::prelude::String;

namespace
{

struct BasicError {
    int value;
};

struct OuterError {
    BasicError inner;
};

struct MissingDebug {};
struct MissingDisplay {};
struct NotAnError {};

struct MoveOnlyError {
    int* drops;
    int  value;

    MoveOnlyError(int& drops, int value): drops(&drops), value(value) {}
    MoveOnlyError(const MoveOnlyError&)            = delete;
    MoveOnlyError& operator=(const MoveOnlyError&) = delete;
    MoveOnlyError(MoveOnlyError&& other) noexcept: drops(other.drops), value(other.value) {
        other.drops = nullptr;
    }
    ~MoveOnlyError() {
        if (drops != nullptr) ++*drops;
    }
};

struct AnySpoof {};

struct EmptyError {};

struct alignas(64) AlignedError {
    int value;
};

} // namespace

namespace rstd
{

template<>
struct Impl<fmt::Display, BasicError> : ImplBase<BasicError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("basic {}", this->self().value));
    }
};

template<>
struct Impl<fmt::Debug, BasicError> : ImplBase<BasicError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("BasicError({})", this->self().value));
    }
};

template<>
struct Impl<error::Error, BasicError> : DefaultInImpl<error::Error, BasicError> {};

template<>
struct Impl<fmt::Display, OuterError> : ImplBase<OuterError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "outer error";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, OuterError> : ImplBase<OuterError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "OuterError";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, OuterError> : ImplBase<OuterError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return Some(dyn<error::Error>::from_ref(this->self().inner));
    }
};

template<>
struct Impl<fmt::Display, MissingDebug> : ImplBase<MissingDebug> {
    auto fmt(fmt::Formatter&) const -> bool { return true; }
};

template<>
struct Impl<error::Error, MissingDebug> : DefaultInImpl<error::Error, MissingDebug> {};

template<>
struct Impl<fmt::Debug, MissingDisplay> : ImplBase<MissingDisplay> {
    auto fmt(fmt::Formatter&) const -> bool { return true; }
};

template<>
struct Impl<error::Error, MissingDisplay> : DefaultInImpl<error::Error, MissingDisplay> {};

template<>
struct Impl<fmt::Display, NotAnError> : ImplBase<NotAnError> {
    auto fmt(fmt::Formatter&) const -> bool { return true; }
};

template<>
struct Impl<fmt::Debug, NotAnError> : ImplBase<NotAnError> {
    auto fmt(fmt::Formatter&) const -> bool { return true; }
};

template<>
struct Impl<fmt::Display, MoveOnlyError> : ImplBase<MoveOnlyError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("move-only {}", this->self().value));
    }
};

template<>
struct Impl<fmt::Debug, MoveOnlyError> : ImplBase<MoveOnlyError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("MoveOnlyError({})", this->self().value));
    }
};

template<>
struct Impl<error::Error, MoveOnlyError> : DefaultInImpl<error::Error, MoveOnlyError> {};

template<>
struct Impl<any::Any, AnySpoof> : ImplBase<AnySpoof> {
    auto type_id() const noexcept -> any::TypeId { return any::TypeId::of<int>(); }
};

template<>
struct Impl<fmt::Display, EmptyError> : ImplBase<EmptyError> {
    auto fmt(fmt::Formatter& formatter) const -> bool { return formatter.write_raw("empty", 5); }
};

template<>
struct Impl<fmt::Debug, EmptyError> : ImplBase<EmptyError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("EmptyError", 10);
    }
};

template<>
struct Impl<error::Error, EmptyError> : DefaultInImpl<error::Error, EmptyError> {};

template<>
struct Impl<fmt::Display, AlignedError> : ImplBase<AlignedError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("aligned {}", this->self().value));
    }
};

template<>
struct Impl<fmt::Debug, AlignedError> : ImplBase<AlignedError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make("AlignedError({})", this->self().value));
    }
};

template<>
struct Impl<error::Error, AlignedError> : DefaultInImpl<error::Error, AlignedError> {};

} // namespace rstd

static_assert(Impled<BasicError, error::Error>);
static_assert(! Impled<MissingDebug, error::Error>);
static_assert(! Impled<MissingDisplay, error::Error>);
static_assert(! Impled<NotAnError, error::Error>);
static_assert(Impled<convert::Infallible, error::Error>);
static_assert(Impled<str_::Utf8Error, error::Error>);
static_assert(Impled<num::ParseIntError, error::Error>);
static_assert(Impled<num::ParseFloatError, error::Error>);
static_assert(Impled<num::TryFromIntError, error::Error>);
static_assert(Impled<num::TryFromFloatError, error::Error>);
static_assert(Impled<ffi::FromBytesWithNulError, error::Error>);
static_assert(Impled<rstd::alloc::AllocError, error::Error>);
static_assert(Impled<::alloc::string::FromUtf8Error, error::Error>);
static_assert(Impled<::alloc::ffi::NulError, error::Error>);
static_assert(Impled<::alloc::ffi::FromVecWithNulError, error::Error>);
static_assert(Impled<::alloc::ffi::IntoStringError, error::Error>);
static_assert(Impled<io::error::Error, error::Error>);
static_assert(Impled<json::Error, error::Error>);
static_assert(Impled<toml::Error, error::Error>);
static_assert(Impled<argparse::ValueError, error::Error>);
static_assert(Impled<argparse::DefinitionError, error::Error>);
static_assert(Impled<argparse::ParseError, error::Error>);
static_assert(Impled<argparse::MatchAccessError, error::Error>);

TEST(Error, DefaultSourceAndErasedFormatting) {
    auto erased = Box<dyn<error::Error>>::make(BasicError { 7 });

    EXPECT_TRUE(erased->source().is_none());
    EXPECT_EQ(rstd::format("{}", erased.as_ref()), "basic 7"_str);
    EXPECT_EQ(rstd::format("{:?}", erased.as_ref()), "BasicError(7)"_str);
    EXPECT_EQ(rstd::format("{}", erased), "basic 7"_str);
    EXPECT_EQ(rstd::format("{:?}", erased), "BasicError(7)"_str);
}

TEST(Error, SourceBorrowsNestedError) {
    auto erased = Box<dyn<error::Error>>::make(OuterError { BasicError { 11 } });
    auto source = erased->source();

    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(error::is<BasicError>(*source));
    EXPECT_EQ(error::downcast_ref<BasicError>(*source)->as_raw_ptr(),
              static_cast<const BasicError*>(source->as_raw_ptr()));
    EXPECT_EQ((**error::downcast_ref<BasicError>(*source)).value, 11);
    EXPECT_EQ(rstd::format("{}", *source), "basic 11"_str);
}

TEST(Error, BorrowedDowncastUsesConcreteMetadata) {
    auto erased = Box<dyn<error::Error>>::make(BasicError { 13 });

    EXPECT_TRUE(error::is<BasicError>(erased.as_ref()));
    EXPECT_FALSE(error::is<OuterError>(erased.as_ref()));
    EXPECT_TRUE(error::downcast_ref<OuterError>(erased.as_ref()).is_none());

    auto mutable_view = erased.deref_mut();
    EXPECT_EQ(rstd::format("{}", mutable_view), "basic 13"_str);
    EXPECT_EQ(rstd::format("{:?}", mutable_view), "BasicError(13)"_str);

    auto value = error::downcast_mut<BasicError>(erased.deref_mut());
    ASSERT_TRUE(value.is_some());
    (**value).value = 17;
    EXPECT_EQ(rstd::format("{}", erased.as_ref()), "basic 17"_str);
}

TEST(Error, OwningDowncastPreservesAllocationAndDrop) {
    int drops = 0;
    {
        auto erased   = Box<dyn<error::Error>>::make(MoveOnlyError { drops, 19 });
        auto address  = erased.as_ref().as_raw_ptr();
        auto concrete = rstd::move(erased).downcast<MoveOnlyError>();

        ASSERT_TRUE(concrete.is_ok());
        EXPECT_EQ((*concrete).as_mut_ptr().as_raw_ptr(), address);
        EXPECT_EQ((*concrete)->value, 19);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Error, FailedOwningDowncastReturnsUsableBox) {
    int drops = 0;
    {
        auto erased = Box<dyn<error::Error>>::make(MoveOnlyError { drops, 23 });
        auto result = rstd::move(erased).downcast<BasicError>();

        ASSERT_TRUE(result.is_err());
        auto restored = rstd::move(result).unwrap_err();
        EXPECT_EQ(rstd::format("{}", restored), "move-only 23"_str);
        EXPECT_TRUE(restored->source().is_none());
        auto concrete = rstd::move(restored).downcast<MoveOnlyError>();
        ASSERT_TRUE(concrete.is_ok());
        EXPECT_EQ((*concrete)->value, 23);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Error, AnyImplCannotSpoofMetadataDowncast) {
    auto erased = Box<dyn<any::Any>>::make(AnySpoof {});
    EXPECT_EQ(erased->type_id(), any::TypeId::of<AnySpoof>());
    EXPECT_TRUE(any::is<AnySpoof>(erased.as_ref()));
    EXPECT_FALSE(any::is<int>(erased.as_ref()));
}

TEST(Error, DynMetadataHandlesEmptyAlignedAndMovedValues) {
    auto empty = Box<dyn<error::Error>>::make(EmptyError {});
    EXPECT_TRUE(rstd::move(empty).downcast<EmptyError>().is_ok());

    auto aligned = Box<dyn<error::Error>>::make(AlignedError { 31 });
    auto address = aligned.as_ref().as_raw_ptr();
    EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(address) % 64, 0u);
    EXPECT_EQ((**error::downcast_ref<AlignedError>(aligned.as_ref())).value, 31);

    int drops = 0;
    {
        auto values = Vec<Box<dyn<error::Error>>>::make();
        values.push(Box<dyn<error::Error>>::make(MoveOnlyError { drops, 37 }));
        EXPECT_EQ((**error::downcast_ref<MoveOnlyError>(values[usize()].as_ref())).value, 37);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Error, TypeIdentityWorksAcrossModuleBoundary) {
    auto erased = make_cross_module_error(29);
    EXPECT_TRUE(error::is<CrossModuleError>(erased.as_ref()));
    EXPECT_EQ((**error::downcast_ref<CrossModuleError>(erased.as_ref())).value, 29);
    EXPECT_EQ(rstd::format("{}", erased), "cross-module error"_str);
}

TEST(Error, CoreAndAllocErrorsFormatAndExposeSources) {
    auto invalid_utf8 = str_::Utf8Error(usize(3), Some(u8(1)));
    EXPECT_EQ(rstd::format("{}", invalid_utf8),
              "invalid utf-8 sequence of 1 bytes from index 3"_str);
    EXPECT_TRUE(as<error::Error>(invalid_utf8).source().is_none());

    auto nul = ffi::FromBytesWithNulError::interior_nul(usize(4));
    EXPECT_EQ(rstd::format("{}", nul),
              "data provided contains an interior nul byte at byte position 4"_str);

    auto bytes = Vec<u8>::make();
    bytes.push(u8(0xff));
    auto string_result = String::from_utf8(rstd::move(bytes));
    ASSERT_TRUE(string_result.is_err());
    auto from_utf8 = rstd::move(string_result).unwrap_err();
    EXPECT_EQ(rstd::format("{}", from_utf8), "invalid utf-8 sequence of 1 bytes from index 0"_str);
    EXPECT_TRUE(as<error::Error>(from_utf8).source().is_none());

    auto invalid_c_string = Vec<u8>::make();
    invalid_c_string.push(u8(0xff));
    auto c_string    = ::alloc::ffi::CString::from_vec_unchecked(rstd::move(invalid_c_string));
    auto into_string = rstd::move(c_string).into_string();
    ASSERT_TRUE(into_string.is_err());
    auto conversion = rstd::move(into_string).unwrap_err();
    auto source     = as<error::Error>(conversion).source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(error::is<str_::Utf8Error>(*source));
    EXPECT_EQ((**error::downcast_ref<str_::Utf8Error>(*source)).valid_up_to(), usize());
}

TEST(Error, IoJsonAndTomlUseDefaultSource) {
    auto io_error =
        io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::InvalidInput });
    EXPECT_TRUE(as<error::Error>(io_error).source().is_none());
    auto erased = Box<dyn<error::Error>>::make(rstd::move(io_error));
    EXPECT_EQ(rstd::format("{}", erased), "invalid input parameter"_str);

    auto json_result = json::from_str("{"_str);
    ASSERT_TRUE(json_result.is_err());
    auto json_error = rstd::move(json_result).unwrap_err();
    EXPECT_TRUE(as<error::Error>(json_error).source().is_none());

    auto toml_result = toml::from_str("value ="_str);
    ASSERT_TRUE(toml_result.is_err());
    auto toml_error = rstd::move(toml_result).unwrap_err();
    EXPECT_TRUE(as<error::Error>(toml_error).source().is_none());
}

TEST(Error, ArgparseNestedCauseHasSingleDisplayOwner) {
    auto definition = argparse::DefinitionError::InvalidDefaultValue(
        String::make("name"_str), argparse::ValueError::Message(String::make("bad default"_str)));
    auto expected = &definition.as_InvalidDefaultValue().error;
    auto source   = as<error::Error>(definition).source();

    ASSERT_TRUE(source.is_some());
    EXPECT_EQ(source->as_raw_ptr(), expected);
    EXPECT_EQ(rstd::format("{}", definition), "invalid default value for argument 'name'"_str);
    EXPECT_EQ(rstd::format("{}", *source), "bad default"_str);

    auto parse          = argparse::ParseError::InvalidValue(String::make("name"_str),
                                                             ffi::OsString::from("bad"_str),
                                                             usize(2),
                                                             argparse::ValueError::InvalidUtf8());
    auto parse_expected = &parse.as_InvalidValue().error;
    auto parse_source   = as<error::Error>(parse).source();

    ASSERT_TRUE(parse_source.is_some());
    EXPECT_EQ(parse_source->as_raw_ptr(), parse_expected);
    EXPECT_EQ(rstd::format("{}", parse), "invalid value 'bad' for argument 'name'"_str);
    EXPECT_EQ(rstd::format("{}", *parse_source), "argument value is not valid UTF-8"_str);
}
