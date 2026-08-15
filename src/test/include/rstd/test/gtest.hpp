#ifndef RSTD_TEST_GTEST_HPP
#define RSTD_TEST_GTEST_HPP

namespace rstd::test::gtest
{

using TestFunction = void (*)() noexcept;

struct Descriptor {
    const char*  suite;
    const char*  name;
    const char*  file;
    int          line;
    TestFunction function;
    Descriptor*  next {};
};

class Registrar {
public:
    explicit Registrar(Descriptor* descriptor) noexcept;
};

auto registered_head() noexcept -> Descriptor*;

enum class ResultKind : unsigned char
{
    Failure,
    Skip,
};

class Message {
    char          bytes_[1024] {};
    unsigned long length_ {};

public:
    auto append(const char* value, unsigned long length) noexcept -> void;
    auto data() const noexcept -> const char* { return bytes_; }
    auto length() const noexcept -> unsigned long { return length_; }

    auto operator<<(const char* value) noexcept -> Message&;

    template<unsigned long Size>
    auto operator<<(const char (&value)[Size]) noexcept -> Message& {
        append(value, Size - 1);
        return *this;
    }

    auto operator<<(char value) noexcept -> Message&;
    auto operator<<(bool value) noexcept -> Message&;
    auto operator<<(int value) noexcept -> Message&;
    auto operator<<(unsigned int value) noexcept -> Message&;
    auto operator<<(long value) noexcept -> Message&;
    auto operator<<(unsigned long value) noexcept -> Message&;
    auto operator<<(long long value) noexcept -> Message&;
    auto operator<<(unsigned long long value) noexcept -> Message&;
    auto operator<<(float value) noexcept -> Message&;
    auto operator<<(double value) noexcept -> Message&;

    template<typename T>
        requires requires(const T& value) {
            static_cast<const char*>(value.data());
            value.size();
        }
    auto operator<<(const T& value) noexcept -> Message& {
        append(static_cast<const char*>(value.data()), static_cast<unsigned long>(value.size()));
        return *this;
    }

    template<typename T>
    auto operator<<(const T&) noexcept -> Message& {
        append("<value>", 7);
        return *this;
    }
};

class ResultHelper {
    ResultKind  kind_;
    bool        fatal_;
    const char* file_;
    int         line_;
    const char* fallback_;

public:
    ResultHelper(ResultKind  kind,
                 bool        fatal,
                 const char* file,
                 int         line,
                 const char* fallback) noexcept
        : kind_(kind), fatal_(fatal), file_(file), line_(line), fallback_(fallback) {}

    auto operator=(const Message& message) const noexcept -> void;
};

class ScopedTrace {
public:
    explicit ScopedTrace(const Message& message) noexcept;
    ScopedTrace(const ScopedTrace&)                    = delete;
    auto operator=(const ScopedTrace&) -> ScopedTrace& = delete;
    ~ScopedTrace() noexcept;
};

class Expectation {
    bool        success_;
    const char* expression_;
    const char* file_;
    int         line_;
    Message     message_;

public:
    Expectation(bool success, const char* expression, const char* file, int line) noexcept
        : success_(success), expression_(expression), file_(file), line_(line) {}
    Expectation(const Expectation&)                    = delete;
    auto operator=(const Expectation&) -> Expectation& = delete;
    ~Expectation() noexcept;

    template<typename T>
    auto operator<<(const T& value) noexcept -> Expectation& {
        message_ << value;
        return *this;
    }
};

enum class DeathDecision : unsigned char
{
    Skip,
    Execute,
};

auto record_assertion(bool        success,
                      bool        fatal,
                      const char* expression,
                      const char* file,
                      int         line) noexcept -> void;
auto float_equal(double left, double right, bool single_precision) noexcept -> bool;
auto float_near(double left, double right, double absolute_error) noexcept -> bool;
auto death_begin(const char* pattern, const char* file, int line) noexcept -> DeathDecision;
[[noreturn]]
auto death_survived() noexcept -> void;
auto configure_death(const char*   program,
                     unsigned long program_length,
                     const char*   case_name,
                     unsigned long case_length,
                     bool          has_case,
                     unsigned long index) noexcept -> void;
auto death_child_active() noexcept -> bool;
[[noreturn]]
auto finish_death_child() noexcept -> void;

} // namespace rstd::test::gtest

#define RSTD_TEST_CONCAT_INNER_(left, right)     left##right
#define RSTD_TEST_CONCAT_(left, right)           RSTD_TEST_CONCAT_INNER_(left, right)
#define RSTD_TEST_STRINGIFY_INNER_(value)        #value
#define RSTD_TEST_STRINGIFY_(value)              RSTD_TEST_STRINGIFY_INNER_(value)
#define RSTD_TEST_FUNCTION_INNER_(suite, name)   rstd_test_case_##suite##_##name
#define RSTD_TEST_FUNCTION_(suite, name)         RSTD_TEST_FUNCTION_INNER_(suite, name)
#define RSTD_TEST_DESCRIPTOR_INNER_(suite, name) rstd_test_descriptor_##suite##_##name
#define RSTD_TEST_DESCRIPTOR_(suite, name)       RSTD_TEST_DESCRIPTOR_INNER_(suite, name)
#define RSTD_TEST_REGISTRAR_INNER_(suite, name)  rstd_test_registrar_##suite##_##name
#define RSTD_TEST_REGISTRAR_(suite, name)        RSTD_TEST_REGISTRAR_INNER_(suite, name)

#define TEST(suite, name)                                                                  \
    static void RSTD_TEST_FUNCTION_(suite, name)() noexcept;                               \
    namespace                                                                              \
    {                                                                                      \
    ::rstd::test::gtest::Descriptor RSTD_TEST_DESCRIPTOR_(suite, name) {                   \
        RSTD_TEST_STRINGIFY_(suite),       RSTD_TEST_STRINGIFY_(name), __FILE__, __LINE__, \
        &RSTD_TEST_FUNCTION_(suite, name),                                                 \
    };                                                                                     \
    ::rstd::test::gtest::Registrar                                                         \
        RSTD_TEST_REGISTRAR_(suite, name) { &RSTD_TEST_DESCRIPTOR_(suite, name) };         \
    }                                                                                      \
    static void RSTD_TEST_FUNCTION_(suite, name)() noexcept

#define RSTD_TEST_EXPECT_BOOL_(expression, expected) \
    ::rstd::test::gtest::Expectation(                \
        static_cast<bool>(expression) == (expected), #expression, __FILE__, __LINE__)

#define RSTD_TEST_ASSERT_BOOL_(expression, expected)                                             \
    if (const bool rstd_test_ok_ = static_cast<bool>(expression) == (expected); rstd_test_ok_) { \
    } else                                                                                       \
        return ::rstd::test::gtest::ResultHelper(::rstd::test::gtest::ResultKind::Failure,       \
                                                 true,                                           \
                                                 __FILE__,                                       \
                                                 __LINE__,                                       \
                                                 #expression) = ::rstd::test::gtest::Message()

#define RSTD_TEST_EXPECT_BINARY_(left, right, operation, operation_text) \
    ::rstd::test::gtest::Expectation(                                    \
        (left)operation(right), #left " " operation_text " " #right, __FILE__, __LINE__)

#define RSTD_TEST_ASSERT_BINARY_(left, right, operation, operation_text)                   \
    if (const bool rstd_test_ok_ = (left)operation(right); rstd_test_ok_) {                \
    } else                                                                                 \
        return ::rstd::test::gtest::ResultHelper(::rstd::test::gtest::ResultKind::Failure, \
                                                 true,                                     \
                                                 __FILE__,                                 \
                                                 __LINE__,                                 \
                                                 #left " " operation_text " " #right) =    \
                   ::rstd::test::gtest::Message()

#define EXPECT_TRUE(expression)  RSTD_TEST_EXPECT_BOOL_(expression, true)
#define EXPECT_FALSE(expression) RSTD_TEST_EXPECT_BOOL_(expression, false)
#define ASSERT_TRUE(expression)  RSTD_TEST_ASSERT_BOOL_(expression, true)
#define ASSERT_FALSE(expression) RSTD_TEST_ASSERT_BOOL_(expression, false)

#define EXPECT_EQ(left, right) RSTD_TEST_EXPECT_BINARY_(left, right, ==, "==")
#define EXPECT_NE(left, right) RSTD_TEST_EXPECT_BINARY_(left, right, !=, "!=")
#define EXPECT_GE(left, right) RSTD_TEST_EXPECT_BINARY_(left, right, >=, ">=")
#define EXPECT_GT(left, right) RSTD_TEST_EXPECT_BINARY_(left, right, >, ">")
#define EXPECT_LE(left, right) RSTD_TEST_EXPECT_BINARY_(left, right, <=, "<=")
#define EXPECT_LT(left, right) RSTD_TEST_EXPECT_BINARY_(left, right, <, "<")
#define ASSERT_EQ(left, right) RSTD_TEST_ASSERT_BINARY_(left, right, ==, "==")
#define ASSERT_NE(left, right) RSTD_TEST_ASSERT_BINARY_(left, right, !=, "!=")
#define ASSERT_GE(left, right) RSTD_TEST_ASSERT_BINARY_(left, right, >=, ">=")
#define ASSERT_GT(left, right) RSTD_TEST_ASSERT_BINARY_(left, right, >, ">")
#define ASSERT_LE(left, right) RSTD_TEST_ASSERT_BINARY_(left, right, <=, "<=")
#define ASSERT_LT(left, right) RSTD_TEST_ASSERT_BINARY_(left, right, <, "<")

#define RSTD_TEST_EXPECT_FLOAT_(left, right, single_precision)                                    \
    ::rstd::test::gtest::Expectation(::rstd::test::gtest::float_equal(static_cast<double>(left),  \
                                                                      static_cast<double>(right), \
                                                                      single_precision),          \
                                     #left " approximately equals " #right,                       \
                                     __FILE__,                                                    \
                                     __LINE__)

#define EXPECT_FLOAT_EQ(left, right)  RSTD_TEST_EXPECT_FLOAT_(left, right, true)
#define EXPECT_DOUBLE_EQ(left, right) RSTD_TEST_EXPECT_FLOAT_(left, right, false)
#define EXPECT_NEAR(left, right, error)                                                           \
    ::rstd::test::gtest::Expectation(::rstd::test::gtest::float_near(static_cast<double>(left),   \
                                                                     static_cast<double>(right),  \
                                                                     static_cast<double>(error)), \
                                     #left " is near " #right,                                    \
                                     __FILE__,                                                    \
                                     __LINE__)

#define FAIL()                                                                                 \
    return ::rstd::test::gtest::ResultHelper(                                                  \
               ::rstd::test::gtest::ResultKind::Failure, true, __FILE__, __LINE__, "Failed") = \
               ::rstd::test::gtest::Message()
#define ADD_FAILURE()                                                                    \
    ::rstd::test::gtest::ResultHelper(                                                   \
        ::rstd::test::gtest::ResultKind::Failure, false, __FILE__, __LINE__, "Failed") = \
        ::rstd::test::gtest::Message()
#define GTEST_SKIP()                                                                          \
    return ::rstd::test::gtest::ResultHelper(                                                 \
               ::rstd::test::gtest::ResultKind::Skip, false, __FILE__, __LINE__, "Skipped") = \
               ::rstd::test::gtest::Message()
#define SCOPED_TRACE(message)                                                        \
    ::rstd::test::gtest::ScopedTrace RSTD_TEST_CONCAT_(rstd_test_trace_, __LINE__) { \
        ::rstd::test::gtest::Message() << message                                    \
    }
#define SUCCEED() \
    do {          \
    } while (false)

#define EXPECT_DEATH(statement, pattern)                                                \
    do {                                                                                \
        auto rstd_test_death_decision_ =                                                \
            ::rstd::test::gtest::death_begin((pattern), __FILE__, __LINE__);            \
        if (rstd_test_death_decision_ == ::rstd::test::gtest::DeathDecision::Execute) { \
            statement;                                                                  \
            ::rstd::test::gtest::death_survived();                                      \
        }                                                                               \
    } while (false)

#endif
