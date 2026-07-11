#include <gtest/gtest.h>

import rstd.json;

using namespace rstd::prelude;
using rstd::json::Array;
using rstd::json::Map;
using rstd::json::Number;
using rstd::json::Value;
using ::alloc::string::String;

TEST(JsonValue, ExposesVariantAccessors) {
    auto null = Value::Null();
    EXPECT_TRUE(null.is_null());
    EXPECT_TRUE(null.as_null().is_some());

    auto boolean = Value::Bool(true);
    EXPECT_TRUE(boolean.is_boolean());
    EXPECT_EQ(boolean.as_bool(), Some(true));

    auto number = Value::Number(Number::from_i64(-7));
    EXPECT_TRUE(number.is_number());
    EXPECT_EQ(number.as_i64(), Some(i64(-7)));

    auto text = Value::String(String::make("hello"));
    EXPECT_TRUE(text.is_string());
    ASSERT_TRUE(text.as_str().is_some());
    EXPECT_EQ(*text.as_str(), rstd::ref<rstd::str>("hello"));
}

TEST(JsonValue, ClonesRecursiveOwnership) {
    auto array = Array::make();
    array.push(Value::String(String::make("first")));

    auto object = Map::make();
    object.insert(String::make("items"), Value::Array(rstd::move(array)));
    auto value = Value::Object(rstd::move(object));

    auto cloned = rstd::as<rstd::clone::Clone>(value).clone();
    EXPECT_EQ(value, cloned);

    auto cloned_items = cloned.get_mut(rstd::ref<rstd::str>("items"));
    ASSERT_TRUE(cloned_items.is_some());
    **cloned_items = Value::Null();

    EXPECT_NE(value, cloned);
    EXPECT_TRUE((**value.get(rstd::ref<rstd::str>("items"))).is_array());
}

TEST(JsonValue, TakeLeavesNull) {
    auto value = Value::String(String::make("owned"));
    auto taken = value.take();

    EXPECT_TRUE(value.is_null());
    EXPECT_EQ(*taken.as_str(), rstd::ref<rstd::str>("owned"));
}

TEST(JsonValue, IndexingMatchesReadAndWriteSemantics) {
    auto value      = Value::Null();
    value["a"]["b"] = Value::Bool(true);

    EXPECT_EQ(value["a"]["b"], true);
    const auto& read = value;
    EXPECT_TRUE(read["missing"].is_null());
    EXPECT_TRUE(read["a"]["missing"].is_null());

    auto array = Array::make();
    array.push(Value::String(String::make("first")));
    auto array_value = Value::Array(rstd::move(array));
    EXPECT_EQ(array_value[usize(0)], rstd::ref<rstd::str>("first"));
    const auto& array_read = array_value;
    EXPECT_TRUE(array_read[usize(1)].is_null());
}

TEST(JsonValue, JsonPointerFollowsRfc6901Tokens) {
    auto value = rstd::json::from_str(R"({"foo":["bar","baz"],"":0,"a/b":1,"m~n":2})").unwrap();

    EXPECT_EQ(**value.pointer(""), value);
    EXPECT_EQ(**value.pointer("/foo/0"), rstd::ref<rstd::str>("bar"));
    EXPECT_EQ(**value.pointer("/"), 0);
    EXPECT_EQ(**value.pointer("/a~1b"), 1);
    EXPECT_EQ(**value.pointer("/m~0n"), 2);
    EXPECT_TRUE(value.pointer("foo").is_none());
    EXPECT_TRUE(value.pointer("/foo/00").is_none());
    EXPECT_TRUE(value.pointer("/foo/01").is_none());
    EXPECT_TRUE(value.pointer("/foo/-").is_none());
    EXPECT_TRUE(value.pointer("/foo/184467440737095516160").is_none());

    **value.pointer_mut("/foo/1") = Value::String(String::make("changed"));
    EXPECT_EQ(**value.pointer("/foo/1"), rstd::ref<rstd::str>("changed"));
}

TEST(JsonValue, PrimitiveConversionsDoNotUseSerialization) {
    auto integer = rstd::Impl<rstd::convert::From<i32>, Value>::from(-9);
    auto text    = rstd::Impl<rstd::convert::From<rstd::ref<rstd::str>>, Value>::from("text");
    auto some    = rstd::Impl<rstd::convert::From<rstd::Option<i32>>, Value>::from(Some(i32(7)));
    auto none =
        rstd::Impl<rstd::convert::From<rstd::Option<i32>>, Value>::from(rstd::Option<i32>());
    auto non_finite = rstd::Impl<rstd::convert::From<f64>, Value>::from(rstd::f64_::NAN_);

    EXPECT_EQ(integer, -9);
    EXPECT_EQ(text, rstd::ref<rstd::str>("text"));
    EXPECT_EQ(some, 7);
    EXPECT_TRUE(none.is_null());
    EXPECT_TRUE(non_finite.is_null());
}

TEST(JsonValue, ObjectIterationAndRecursiveSortAreStable) {
    auto value = rstd::json::from_str(R"({"z":{"d":1,"c":2},"a":0,"m":[]})").unwrap();
    value.sort_all_objects();

    auto keys = (**value.as_object()).keys();
    EXPECT_EQ(**keys.next(), "a");
    EXPECT_EQ(**keys.next(), "m");
    EXPECT_EQ(**keys.next(), "z");
    EXPECT_TRUE(keys.next().is_none());

    auto nested      = (**value.get("z")).as_object();
    auto nested_keys = (**nested).keys();
    EXPECT_EQ(**nested_keys.next(), "c");
    EXPECT_EQ(**nested_keys.next(), "d");
}
