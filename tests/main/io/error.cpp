#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{
struct OwnedCause {
    int* drops;
    int  value;
    OwnedCause(int& drops, int value): drops(&drops), value(value) {}
    OwnedCause(const OwnedCause&) = delete;
    OwnedCause(OwnedCause&& other) noexcept
        : drops(rstd::exchange(other.drops, nullptr)), value(other.value) {}
    ~OwnedCause() {
        if (drops) ++*drops;
    }
};

struct ContextCause {
    Box<dyn<error::Error>> inner;
};

} // namespace

namespace rstd
{
template<>
struct Impl<fmt::Display, OwnedCause> : ImplBase<OwnedCause> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_fmt(fmt::Arguments::make("owned cause {}", self().value));
    }
};
template<>
struct Impl<fmt::Debug, OwnedCause> : ImplBase<OwnedCause> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_fmt(fmt::Arguments::make("OwnedCause({})", self().value));
    }
};
template<>
struct Impl<error::Error, OwnedCause> : DefaultInImpl<error::Error, OwnedCause> {};

template<>
struct Impl<fmt::Display, ContextCause> : ImplBase<ContextCause> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.write_str("context"_str); }
};
template<>
struct Impl<fmt::Debug, ContextCause> : ImplBase<ContextCause> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.write_str("ContextCause"_str); }
};
template<>
struct Impl<error::Error, ContextCause> : ImplBase<ContextCause> {
    auto source() const noexcept [[clang::lifetimebound]] -> Option<error::ErrorRef> {
        return Some(self().inner.as_ref());
    }
};
} // namespace rstd

TEST(IoError, BoxedPayloadMovesWithoutCloningAndDropsOnce) {
    int drops {};
    {
        auto original = io::Error::with_source(io::ErrorKind { io::ErrorKind::TimedOut },
                                               Box<dyn<error::Error>>::make(OwnedCause(drops, 41)));
        auto assigned = io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput });
        assigned      = rstd::move(original);
        auto moved    = rstd::move(assigned);
        EXPECT_EQ(drops, 0);
        auto typed = error::downcast_ref<OwnedCause>(moved.get_ref().unwrap());
        ASSERT_TRUE(typed.is_some());
        EXPECT_EQ((*typed)->value, 41);
        EXPECT_EQ(moved.kind(), io::ErrorKind { io::ErrorKind::TimedOut });
        EXPECT_TRUE(as<error::Error>(moved).source().is_none());
        EXPECT_EQ(rstd::format("{}", moved), "owned cause 41"_str);
        EXPECT_EQ(rstd::format("{:?}", moved),
                  "Custom { kind: timed out, error: OwnedCause(41) }"_str);
    }
    EXPECT_EQ(drops, 1);
}

TEST(IoError, GetRefReturnsPayloadWhileSourceDelegatesToPayloadSource) {
    int drops {};
    {
        auto value =
            io::Error::with_source(io::ErrorKind { io::ErrorKind::ConnectionReset },
                                   Box<dyn<error::Error>>::make(ContextCause {
                                       Box<dyn<error::Error>>::make(OwnedCause(drops, 7)) }));
        EXPECT_TRUE(error::downcast_ref<ContextCause>(value.get_ref().unwrap()).is_some());
        auto source = as<error::Error>(value).source().unwrap();
        auto leaf   = error::downcast_ref<OwnedCause>(source).unwrap();
        EXPECT_EQ(leaf->value, 7);
        EXPECT_TRUE(as<error::Error>(*leaf).source().is_none());
        EXPECT_EQ(rstd::format("{}", value), "context"_str);
        EXPECT_TRUE(value.raw_os_error().is_none());
        EXPECT_EQ(value.static_message(), nullptr);
    }
    EXPECT_EQ(drops, 1);
}

TEST(IoError, GetMutAndIntoInnerPreserveUniqueOwnership) {
    int drops {};
    {
        auto value   = io::Error::with_source(io::ErrorKind { io::ErrorKind::InvalidData },
                                              Box<dyn<error::Error>>::make(OwnedCause(drops, 5)));
        auto typed   = error::downcast_mut<OwnedCause>(value.get_mut().unwrap()).unwrap();
        typed->value = 9;
        auto payload = rstd::move(value).into_inner().unwrap();
        EXPECT_EQ(error::downcast_ref<OwnedCause>(payload.as_ref()).unwrap()->value, 9);
        EXPECT_TRUE(value.get_ref().is_none());
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
}

TEST(IoError, NestedIoErrorsExposePayloadSeparatelyFromSource) {
    int drops {};
    {
        auto inner  = io::Error::with_source(io::ErrorKind { io::ErrorKind::InvalidData },
                                             Box<dyn<error::Error>>::make(OwnedCause(drops, 7)));
        auto outer  = io::Error::with_source(io::ErrorKind { io::ErrorKind::ConnectionReset },
                                             Box<dyn<error::Error>>::make(rstd::move(inner)));
        auto nested = error::downcast_ref<io::Error>(outer.get_ref().unwrap()).unwrap();
        EXPECT_EQ(nested->kind(), io::ErrorKind { io::ErrorKind::InvalidData });
        auto leaf = error::downcast_ref<OwnedCause>(nested->get_ref().unwrap()).unwrap();
        EXPECT_EQ(leaf->value, 7);
        EXPECT_TRUE(as<error::Error>(outer).source().is_none());
        EXPECT_EQ(rstd::format("{}", outer), "owned cause 7"_str);
    }
    EXPECT_EQ(drops, 1);
}

TEST(IoError, NativeCodeRemainsAvailableFromTypedInnerError) {
    auto native  = io::Error::from_raw_os_error(i32::MIN);
    auto wrapped = io::Error::with_source(io::ErrorKind { io::ErrorKind::Other },
                                          Box<dyn<error::Error>>::make(rstd::move(native)));
    EXPECT_TRUE(wrapped.raw_os_error().is_none());
    auto inner = error::downcast_ref<io::Error>(wrapped.get_ref().unwrap()).unwrap();
    EXPECT_EQ(inner->raw_os_error().unwrap(), i32::MIN);
    EXPECT_TRUE(as<error::Error>(wrapped).source().is_none());
}

TEST(IoError, LookupMovePreservesOwnedCause) {
    int drops {};
    {
        auto copy = [&] {
            net::LookupError original { i32(7),
                                        String::make("query"_str),
                                        Some(io::Error::with_source(
                                            io::ErrorKind { io::ErrorKind::InvalidData },
                                            Box<dyn<error::Error>>::make(OwnedCause(drops, 13)))) };
            return original;
        }();
        EXPECT_EQ(drops, 0);
        EXPECT_EQ(copy.code, i32(7));
        EXPECT_EQ(copy.message.as_str(), "query"_str);
        auto inner =
            error::downcast_ref<io::Error>(as<error::Error>(copy).source().unwrap()).unwrap();
        auto leaf = error::downcast_ref<OwnedCause>(inner->get_ref().unwrap()).unwrap();
        EXPECT_EQ(leaf->value, 13);
    }
    EXPECT_EQ(drops, 1);
}

TEST(IoError, LiteralAndPlainVariantsKeepExistingSourceSemantics) {
    auto plain   = io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotFound });
    auto message = io::Error::make_const(io::ErrorKind { io::ErrorKind::InvalidData }, "bad data");
    EXPECT_EQ(rstd::format("{}", message), "bad data"_str);
    EXPECT_EQ(rstd::format("{:?}", message),
              "Error { kind: invalid data, message: \"bad data\" }"_str);
    EXPECT_EQ(rstd::format("{:?}", plain), "Kind(entity not found)"_str);
    EXPECT_TRUE(plain.get_ref().is_none());
    EXPECT_TRUE(message.get_ref().is_none());
    EXPECT_TRUE(message.get_mut().is_none());
    EXPECT_TRUE(as<error::Error>(plain).source().is_none());
    EXPECT_TRUE(as<error::Error>(message).source().is_none());
    EXPECT_TRUE(rstd::move(message).into_inner().is_none());
    auto native = io::Error::from_raw_os_error(i32::MIN);
    EXPECT_TRUE(as<error::Error>(native).source().is_none());
}
