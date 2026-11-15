/**
 * @file ufsrvscriptlib_protobuf_test.cpp
 * @brief Adversarial gtest suite for the ufsrvscriptlib Protocol Buffers module.
 *
 * These tests are written to break the protobuf module, not to bless its current
 * behaviour: malformed and truncated wire bytes, unknown types/fields, wrong and
 * missing arguments, boundary varint/int64 values, and the seam's security
 * surface (pb.unsafe/pb.io/pb.loadfile must be absent). The invariant under test
 * is the "no input aborts the process" contract: any malformed input must raise a
 * catchable Lua error, never segfault or trip ASan.
 *
 * The module is exercised against a raw lua_State (full stdlib) in isolation —
 * the engine's sandbox pruning is covered separately in ufsrvscriptlib_lua_test.cpp.
 */
#include <gtest/gtest.h>

extern "C" {
#include <ufsrvscriptlib/ufsrvscriptlib_protobuf.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <string>
#include <vector>

namespace {

class ProtobufFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        L_ = luaL_newstate();
        ASSERT_NE(L_, nullptr);
        luaL_openlibs(L_);
        UfsrvScriptProtobufRegister(L_);

        /* One schema covering all scalar wire types, plus nested/repeated. */
        auto r = RunChunk(
            "protoc:load [[\n"
            "  message Scalar {\n"
            "    optional int32    i32 = 1;\n"
            "    optional int64    i64 = 2;\n"
            "    optional uint32   u32 = 3;\n"
            "    optional uint64   u64 = 4;\n"
            "    optional sint32   s32 = 5;\n"
            "    optional sint64   s64 = 6;\n"
            "    optional fixed64  f64 = 7;\n"
            "    optional sfixed32 sf32 = 8;\n"
            "    optional double   dbl = 9;\n"
            "    optional float    flt = 10;\n"
            "    optional bool     boo = 11;\n"
            "    optional string   str = 12;\n"
            "    optional bytes    byt = 13;\n"
            "  }\n"
            "  message Nested { optional Scalar inner = 1; repeated int32 nums = 2; }\n"
            "  message Repeat { repeated string tags = 1; repeated Scalar items = 2; }\n"
            "]]");
        ASSERT_TRUE(r.first) << "schema load failed: " << r.second;
    }

    void TearDown() override
    {
        lua_close(L_);
        L_ = nullptr;
    }

    std::string ValStr(int idx)
    {
        switch (lua_type(L_, idx)) {
            case LUA_TNIL:     return "nil";
            case LUA_TBOOLEAN: return lua_toboolean(L_, idx) ? "true" : "false";
            case LUA_TNUMBER:  return lua_tostring(L_, idx);
            case LUA_TSTRING: {
                size_t len = 0;
                const char *s = lua_tolstring(L_, idx, &len);
                return std::string(s, len);
            }
            case LUA_TTABLE:   return "table";
            default:           return lua_typename(L_, lua_type(L_, idx));
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

    /* pb.decode(type, raw_bytes) under pcall. A "RAISED:" prefix means the
     * decoder raised; otherwise it is the stringified decoded value. */
    std::pair<bool, std::string> DecodeRaw(const char *type, const std::string &bytes)
    {
        lua_settop(L_, 0);
        lua_getglobal(L_, "pb");
        lua_getfield(L_, -1, "decode");
        lua_remove(L_, -2);
        lua_pushstring(L_, type);
        lua_pushlstring(L_, bytes.data(), bytes.size());
        if (lua_pcall(L_, 2, LUA_MULTRET, 0) != LUA_OK) {
            std::string m = "RAISED: ";
            m += lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(no message)";
            lua_settop(L_, 0);
            return { false, m };
        }
        int n = lua_gettop(L_);
        std::string v = (n >= 1 && !lua_isnil(L_, 1)) ? ValStr(1) : "";
        lua_settop(L_, 0);
        return { true, v };
    }

    lua_State *L_ = nullptr;
};

TEST_F(ProtobufFixture, GlobalsPresent)
{
    EXPECT_EQ(RunChunk("return type(pb), type(protoc), type(serpent)").second,
              "table|table|table");
}

/* The seam must NOT expose the raw-pointer / filesystem surfaces. */
TEST_F(ProtobufFixture, SecuritySurfacesAbsent)
{
    EXPECT_EQ(RunChunk("return pb.unsafe, pb.io, pb.loadfile").second, "nil|nil|nil");
}

TEST_F(ProtobufFixture, RoundTripScalars)
{
    auto r = RunChunk(
        "local b = pb.encode('Scalar', {i32=42, i64=1234567890123, str='hi', boo=true, dbl=1.5})"
        " local d = pb.decode('Scalar', b)"
        " return d.i32, d.i64, d.str, d.boo, d.dbl");
    EXPECT_EQ(r.second, "42|1234567890123|hi|true|1.5");
}

TEST_F(ProtobufFixture, RoundTripBoundaryValues)
{
    /* int32/uint32/int64 max and an int64-range uint64 all fit Lua's int64. */
    auto r = RunChunk(
        "local t = {i32=2147483647, u32=4294967295, i64=9223372036854775807, u64=9223372036854775807}"
        " local d = pb.decode('Scalar', pb.encode('Scalar', t))"
        " return d.i32, d.u32, d.i64, d.u64");
    EXPECT_EQ(r.second, "2147483647|4294967295|9223372036854775807|9223372036854775807");
}

TEST_F(ProtobufFixture, Uint64BeyondInt64RaisesCleanly)
{
    /* uint64 max is not representable as a Lua integer; encode must raise a
     * catchable error, not crash or silently clamp. */
    EXPECT_EQ(RunChunk("return (pcall(function() pb.encode('Scalar', {u64=18446744073709551615}) end))").second,
              "false");
}

TEST_F(ProtobufFixture, RoundTripNegativeAndSint)
{
    auto r = RunChunk(
        "local t = {i32=-2147483648, i64=-9223372036854775807, s32=-123456, s64=-1234567890123}"
        " local d = pb.decode('Scalar', pb.encode('Scalar', t))"
        " return d.i32, d.i64, d.s32, d.s64");
    EXPECT_EQ(r.second, "-2147483648|-9223372036854775807|-123456|-1234567890123");
}

TEST_F(ProtobufFixture, RepeatedAndNested)
{
    auto r = RunChunk(
        "local b = pb.encode('Repeat', { tags={'a','b','c'}, items={{i32=1},{i32=2},{str='x'}} })"
        " local d = pb.decode('Repeat', b)"
        " return d.tags[1], d.tags[3], d.items[2].i32, d.items[3].str");
    EXPECT_EQ(r.second, "a|c|2|x");
}

TEST_F(ProtobufFixture, NestedMessageRoundTrip)
{
    auto r = RunChunk(
        "local b = pb.encode('Nested', { inner={i32=7, str='n'}, nums={1,2,3} })"
        " local d = pb.decode('Nested', b)"
        " return d.inner.i32, d.inner.str, d.nums[1], d.nums[3]");
    EXPECT_EQ(r.second, "7|n|1|3");
}

TEST_F(ProtobufFixture, BytesFieldRoundTrip)
{
    auto r = RunChunk(
        "local b = pb.encode('Scalar', {byt='A\\0B\\255C'})"
        " local d = pb.decode('Scalar', b)"
        " return #d.byt, d.byt:byte(1), d.byt:byte(2), d.byt:byte(4), d.byt:byte(5)");
    EXPECT_EQ(r.second, "5|65|0|255|67");
}

/* Unknown type name must raise a catchable error, not abort. */
TEST_F(ProtobufFixture, UnknownTypeRaises)
{
    EXPECT_EQ(RunChunk("return (pcall(function() pb.encode('NoSuchType', {}) end))").second, "false");
    EXPECT_EQ(RunChunk("return (pcall(function() pb.decode('NoSuchType', '') end))").second, "false");
}

TEST_F(ProtobufFixture, MissingArgsRaise)
{
    EXPECT_EQ(RunChunk("return (pcall(function() pb.encode() end))").second, "false");
    EXPECT_EQ(RunChunk("return (pcall(function() pb.decode() end))").second, "false");
}

TEST_F(ProtobufFixture, BadTableArgRaises)
{
    EXPECT_EQ(RunChunk("return (pcall(function() pb.encode('Scalar', 'not-a-table') end))").second, "false");
    EXPECT_EQ(RunChunk("return (pcall(function() pb.encode('Scalar', 42) end))").second, "false");
}

/* pb.load is the one reporting loader: garbage returns (false, offset). */
TEST_F(ProtobufFixture, LoadGarbageReports)
{
    auto r = RunChunk("local ok, off = pb.load('definitely not a schema') return ok, off ~= nil");
    EXPECT_TRUE(r.first) << "pb.load(garbage) must not raise: " << r.second;
    EXPECT_EQ(RunChunk("local ok = pb.load('definitely not a schema') return ok").second, "false");
}

TEST_F(ProtobufFixture, LoadEmptyReports)
{
    /* Empty schema data is a no-op success — it must not raise or crash. */
    EXPECT_EQ(RunChunk("return (pcall(function() pb.load('') end))").second, "true");
}

/* The decoder must never segfault/ASan-crash on hostile wire bytes — it may
 * decode garbage or raise, but the process must survive every one. */
TEST_F(ProtobufFixture, MalformedDecodeNeverCrashes)
{
    const std::vector<std::string> malformed = {
        std::string("\xFF\xFF\xFF", 3),                       /* field 31, wiretype 7 (invalid) */
        std::string("\x80", 1),                               /* truncated varint */
        std::string("\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80", 10), /* overlong varint */
        std::string("\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x01", 11), /* 11-byte varint */
        std::string("\x0A\x05\x61", 3),                       /* len-delimited, len 5, 1 byte */
        std::string("\x0A\xFF\xFF\xFF\xFF\x7F", 6),           /* huge length prefix */
        std::string("\x0A", 1),                               /* tag without length/bytes */
        std::string("\x00", 1),                               /* field 0, wiretype 0 */
        std::string("\x12\x00", 2),                           /* empty submessage */
        std::string("\x1F", 1),                               /* field 3, wiretype 7 */
        std::string("\x0F\x7F", 2),                           /* field 1, fixed32 truncated */
    };
    for (const auto &bytes : malformed) {
        DecodeRaw("Scalar", bytes);   /* must not abort the process */
    }
    SUCCEED();
}

/* Unknown field numbers are skipped; known fields still decode. */
TEST_F(ProtobufFixture, UnknownFieldIgnoredOnDecode)
{
    /* field 99 (varint) value 1 = 0x98 0x06 0x01; not in the Scalar schema. */
    auto r = DecodeRaw("Scalar", std::string("\x98\x06\x01", 3));
    EXPECT_TRUE(r.first);
    EXPECT_EQ(RunChunk("local d = pb.decode('Scalar', '\\152\\6\\1') return d.i32, d.str").second,
              "nil|nil");
}

/* Unknown keys in the input table are ignored on encode. */
TEST_F(ProtobufFixture, ExtraKeyIgnoredOnEncode)
{
    auto r = RunChunk(
        "local b = pb.encode('Scalar', {i32=7, bogus=123, also_bogus={1,2}})"
        " local d = pb.decode('Scalar', b) return d.i32, d.bogus");
    EXPECT_EQ(r.second, "7|nil");
}

TEST_F(ProtobufFixture, TohexFromhexRoundTrip)
{
    /* pb.tohex emits space-separated bytes; pb.fromhex accepts compact hex. */
    auto r = RunChunk("return pb.tohex('abc'), pb.fromhex('616263')");
    EXPECT_EQ(r.second, "61 62 63|abc");
}

TEST_F(ProtobufFixture, VarintEdges)
{
    auto r = RunChunk(
        "local t = {u64=0}"
        " local ok = true"
        " for _, v in ipairs({0, 127, 128, 16383, 16384, 2097151, 2097152}) do"
        "   t.u64 = v"
        "   local d = pb.decode('Scalar', pb.encode('Scalar', t))"
        "   if d.u64 ~= v then ok = false end"
        " end"
        " return ok");
    EXPECT_EQ(r.second, "true");
}

TEST_F(ProtobufFixture, DoubleRoundTrip)
{
    auto r = RunChunk(
        "local t = {dbl=3.14159265358979}"
        " local d = pb.decode('Scalar', pb.encode('Scalar', t))"
        " return math.abs(d.dbl - 3.14159265358979) < 1e-9");
    EXPECT_EQ(r.second, "true");
}

}  // namespace
