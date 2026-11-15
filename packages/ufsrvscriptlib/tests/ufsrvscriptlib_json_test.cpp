/**
 * @file ufsrvscriptlib_json_test.cpp
 * @brief Adversarial gtest suite for the ufsrvscriptlib JSON module.
 *
 * These tests are written to break the json module, not to bless its current
 * behaviour: malformed and out-of-range input, surrogate-pair edge cases,
 * depth-limit trips, unencodable values, instance isolation, and the "no input
 * aborts the process" contract enforced by the UfsrvScriptJsonFatal funnel.
 *
 * The module is tested against a raw lua_State (full stdlib) in isolation — the
 * engine's sandbox pruning is covered separately in ufsrvscriptlib_lua_test.cpp.
 */
#include <gtest/gtest.h>

extern "C" {
#include <ufsrvscriptlib/ufsrvscriptlib_json.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>

namespace {

class JsonFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        L_ = luaL_newstate();
        ASSERT_NE(L_, nullptr);
        luaL_openlibs(L_);
        UfsrvScriptJsonRegister(L_);
    }

    void TearDown() override
    {
        lua_close(L_);
        L_ = nullptr;
    }

    /* Stringify the value at absolute stack index `idx`. */
    std::string ValStr(int idx)
    {
        switch (lua_type(L_, idx)) {
            case LUA_TNIL:           return "nil";
            case LUA_TBOOLEAN:       return lua_toboolean(L_, idx) ? "true" : "false";
            case LUA_TNUMBER:        return lua_tostring(L_, idx);
            case LUA_TSTRING: {
                size_t len = 0;
                const char *s = lua_tolstring(L_, idx, &len);
                return std::string(s, len);
            }
            case LUA_TTABLE:         return "table";
            case LUA_TFUNCTION:      return "function";
            case LUA_TLIGHTUSERDATA: return "lightuserdata";
            default:                 return lua_typename(L_, lua_type(L_, idx));
        }
    }

    /* Run a chunk; on success return { true, "v1|v2|…" } of all results. */
    std::pair<bool, std::string> RunChunk(const char *chunk)
    {
        if (luaL_dostring(L_, chunk) != LUA_OK) {
            std::string m = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(no message)";
            lua_settop(L_, 0);
            return { false, m };
        }
        int n = lua_gettop(L_);
        std::string out;
        for (int i = 1; i <= n; ++i) {
            if (i > 1)
                out += '|';
            out += ValStr(i);
        }
        lua_settop(L_, 0);
        return { true, out };
    }

    /* Call json.decode(json_text) with exact bytes (no Lua quoting). Returns
     * { ok, value-or-err }. A "RAISED:" prefix means it raised instead of
     * returning nil,err — itself a finding for the "safe" wrapper. */
    std::pair<bool, std::string> Decode(const std::string &json_text)
    {
        lua_settop(L_, 0);
        lua_getglobal(L_, "json");
        lua_getfield(L_, -1, "decode");
        lua_remove(L_, -2);
        lua_pushlstring(L_, json_text.data(), json_text.size());
        if (lua_pcall(L_, 1, LUA_MULTRET, 0) != LUA_OK) {
            std::string m = "RAISED: ";
            m += lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(no message)";
            lua_settop(L_, 0);
            return { false, m };
        }
        int n = lua_gettop(L_);
        bool ok = n >= 1 && !lua_isnil(L_, 1);
        std::string v = ok ? ValStr(1) : (n >= 2 ? ValStr(2) : "");
        lua_settop(L_, 0);
        return { ok, v };
    }

    lua_State *L_ = nullptr;
};

TEST_F(JsonFixture, DecodeScalarTypes)
{
    EXPECT_EQ(Decode("true").second, "true");
    EXPECT_EQ(Decode("false").second, "false");
    EXPECT_EQ(Decode("null").second, "lightuserdata");   /* json.null sentinel */
    EXPECT_EQ(Decode("42").second, "42");
    EXPECT_EQ(Decode("1.5").second, "1.5");
    EXPECT_EQ(Decode("\"hi\"").second, "hi");
}

TEST_F(JsonFixture, DecodeIntegerTypePreserved)
{
    EXPECT_EQ(RunChunk("return math.type(json.decode('42'))").second, "integer");
    EXPECT_EQ(RunChunk("return math.type(json.decode('1.5'))").second, "float");
    EXPECT_EQ(RunChunk("return math.type(json.decode('-9223372036854775808'))").second, "integer");
}

TEST_F(JsonFixture, DecodeObjectAndArray)
{
    EXPECT_EQ(Decode("{\"a\":1}").second, "table");
    EXPECT_EQ(Decode("[1,2,3]").second, "table");
    auto r = RunChunk("local t=json.decode('{\"a\":1,\"b\":[1,2]}') return t.a, t.b[2]");
    EXPECT_EQ(r.second, "1|2");
}

TEST_F(JsonFixture, EncodeArrayExact)
{
    EXPECT_EQ(RunChunk("return json.encode({1,2,3})").second, "[1,2,3]");
    EXPECT_EQ(RunChunk("return json.encode({1,json.null,2})").second, "[1,null,2]");
}

TEST_F(JsonFixture, EncodeObjectRoundTrips)
{
    /* Object key order is not contractual — assert via round-trip, not exact text. */
    auto r = RunChunk(
        "local t={a=1,b='x',c={true,json.null}} local j=json.encode(t)"
        " local u=json.decode(j) return u.a, u.b, u.c[1], u.c[2]==json.null");
    EXPECT_EQ(r.second, "1|x|true|true");
}

TEST_F(JsonFixture, EncodeIntegerVsFloat)
{
    EXPECT_EQ(RunChunk("return json.encode(9223372036854775807)").second, "9223372036854775807");
    EXPECT_EQ(RunChunk("return json.encode(1.5)").second, "1.5");
}

TEST_F(JsonFixture, EncodeUnsupportedTypesReturnNilErr)
{
    EXPECT_EQ(RunChunk("local v,e=json.encode(function() end) return v==nil, e~=nil").second, "true|true");
    EXPECT_EQ(RunChunk("local v,e=json.encode(0/0) return v==nil, e~=nil").second, "true|true");
    EXPECT_EQ(RunChunk("local v,e=json.encode(1/0) return v==nil, e~=nil").second, "true|true");
}

TEST_F(JsonFixture, EncodeSparseArrays)
{
    EXPECT_EQ(RunChunk("return json.encode({[1]='a',[3]='c'})").second, "[\"a\",null,\"c\"]");
    EXPECT_EQ(RunChunk("local v,e=json.encode({[1000000]=1}) return v==nil, e~=nil").second, "true|true");
}

TEST_F(JsonFixture, EmptyTableObjectVsArray)
{
    EXPECT_EQ(RunChunk("return json.encode({})").second, "{}");
    EXPECT_EQ(RunChunk("json.encode_empty_table_as_object(false) return json.encode({})").second, "[]");
    EXPECT_EQ(RunChunk("return json.encode(json.empty_array)").second, "[]");
}

TEST_F(JsonFixture, MalformedInputNeverCrashesNorRaises)
{
    static const char *const malformed[] = {
        "", " ", "{", "}", "[", "]", "{\"a\":1", "[1,2", "1 2", "{} x",
        "\"unterminated", "\"\\q\"", "\"\\u12\"", "\"\\uD800\"",
        "nul", "tru", "falsee", "[1,]", "{\"a\":}", "{1:2}", "[\"a\" \"b\"]",
        "t", "f", "n", "\"\\uZZZZ\"",
    };
    for (const char *s : malformed) {
        auto r = Decode(s);
        EXPECT_FALSE(r.first) << "decode('" << s << "') should fail";
        EXPECT_FALSE(r.second.empty()) << "decode('" << s << "') should report an error";
        EXPECT_TRUE(r.second.rfind("RAISED:", 0) != 0)
            << "decode('" << s << "') must return nil,err — not raise";
    }
}

TEST_F(JsonFixture, StrictNumbersRejectedWhenDisabled)
{
    ASSERT_TRUE(RunChunk("json.decode_invalid_numbers(false)").first);
    for (const char *s : {"01", "+1", "0x1F", "NaN", "Infinity", "-Infinity", "0x10"}) {
        auto r = Decode(s);
        EXPECT_FALSE(r.first) << "strict decode of '" << s << "' should error";
        EXPECT_TRUE(r.second.rfind("RAISED:", 0) != 0)
            << "strict decode of '" << s << "' should return nil,err — not raise";
    }
}

TEST_F(JsonFixture, DepthLimitCleanError)
{
    std::string deep(2000, '[');
    deep.append(2000, ']');
    auto r = Decode(deep);
    EXPECT_FALSE(r.first);
    EXPECT_FALSE(r.second.empty());
    EXPECT_TRUE(r.second.rfind("RAISED:", 0) != 0);

    std::string shallow(100, '[');
    shallow.append(100, ']');
    EXPECT_EQ(Decode(shallow).second, "table");
}

TEST_F(JsonFixture, SurrogatePairs)
{
    EXPECT_FALSE(Decode("\"\\uD800\"").first);          /* high surrogate alone */
    EXPECT_FALSE(Decode("\"\\uDC00\"").first);          /* low surrogate alone */
    EXPECT_FALSE(Decode("\"\\uD800\\u0041\"").first);   /* high + non-surrogate */
    auto r = Decode("\"\\uD834\\uDD1E\"");              /* valid pair → U+1D11E */
    EXPECT_TRUE(r.first);
    EXPECT_EQ(r.second, "\xF0\x9D\x84\x9E");            /* UTF-8 of 𝄞 */
}

TEST_F(JsonFixture, NewInstanceIsolation)
{
    auto r = RunChunk("local j=json.new() j.encode_max_depth(1) return json.encode_max_depth(), j.encode_max_depth()");
    EXPECT_EQ(r.second, "1000|1");
}

TEST_F(JsonFixture, SettingOutOfRangeRaises)
{
    EXPECT_EQ(RunChunk("return (pcall(function() json.encode_max_depth(0) end))").second, "false");
    EXPECT_EQ(RunChunk("return (pcall(function() json.decode_max_depth(0) end))").second, "false");
    EXPECT_EQ(RunChunk("return (pcall(function() json.encode_number_precision(17) end))").second, "false");
}

TEST_F(JsonFixture, IntegerOverflowFallsBackToFloat)
{
    /* Exact int64 bounds round-trip as integers. */
    EXPECT_EQ(Decode("9223372036854775807").second, "9223372036854775807");
    EXPECT_EQ(RunChunk("return math.type(json.decode('-9223372036854775808'))").second, "integer");

    /* Just past int64 must fall back to a float, not silently clamp to
     * LLONG_MAX / LLONG_MIN. */
    EXPECT_EQ(RunChunk("return math.type(json.decode('9223372036854775808'))").second, "float");
    EXPECT_EQ(RunChunk("return math.type(json.decode('-9223372036854775809'))").second, "float");
    EXPECT_EQ(RunChunk("return math.type(json.decode('999999999999999999999999999999'))").second, "float");
}

}  // namespace
