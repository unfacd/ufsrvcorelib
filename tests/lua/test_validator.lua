#!/usr/bin/env lua
-- test_validator.lua — standalone test suite for core_schema_validator.lua
--
-- Run:  lua tests/lua/test_validator.lua
--
-- Tests are grouped by function (validate_value, validate_table, merge_schemas,
-- validate_config).  Each test prints "OK" or "FAIL" plus detail.  Non-zero
-- exit on any failure.
--
-- Copyright (C) 2015-2026 unfacd works
-- SPDX-License-Identifier: AGPL-3.0-or-later

local v = require          -- not used; dofile below for absolute-path safety
-- Resolve paths relative to this script's directory
local script_dir = debug.getinfo(1, "S").source:match("@(.*/)") or "./"
local etc_dir    = script_dir .. "../../etc/"
local fix_dir    = script_dir .. "fixtures/"

local function fixture(name)
    return fix_dir .. name
end

-- Load the validator
local ok, validator = pcall(dofile, etc_dir .. "core_schema_validator.lua")
if not ok then
    io.stderr:write("FATAL: could not load validator: " .. tostring(validator) .. "\n")
    os.exit(1)
end

-- ── Test harness ─────────────────────────────────────────────────────────

local passed, failed = 0, 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        print("  PASS  " .. name)
    else
        failed = failed + 1
        print("  FAIL  " .. name .. "  — " .. tostring(err))
    end
end

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("%s: expected %s, got %s",
              msg or "assertion failed", tostring(expected), tostring(actual)))
    end
end

local function assert_match(str, pattern, msg)
    if not str:match(pattern) then
        error(string.format("%s: pattern '%s' not found in '%s'",
              msg or "assertion failed", pattern, tostring(str)))
    end
end

local function assert_err(fn, pattern, msg)
    local ok, err = pcall(fn)
    if ok then
        error((msg or "expected error") .. ": no error raised")
    end
    if pattern and not tostring(err):match(pattern) then
        error(string.format("%s: expected '%s', got '%s'",
              msg or "pattern mismatch", pattern, tostring(err)))
    end
end

-- ══════════════════════════════════════════════════════════════════════════
-- Group 1 — validate_value
-- ══════════════════════════════════════════════════════════════════════════

print("\n── Group 1: validate_value ──")

test("string value passes", function()
    local r = validator.validate_value("hello", { type = "string" }, {"k"})
    assert_eq(r, "hello")
end)

test("integer value passes", function()
    local r = validator.validate_value(42, { type = "integer" }, {"k"})
    assert_eq(r, 42)
end)

test("float is not integer", function()
    assert_err(function()
        validator.validate_value(3.14, { type = "integer" }, {"k"})
    end, "must be integer")
end)

test("boolean value passes", function()
    local r = validator.validate_value(true, { type = "boolean" }, {"k"})
    assert_eq(r, true)
end)

test("required field missing raises error", function()
    assert_err(function()
        validator.validate_value(nil, { type = "string", required = true }, {"scope", "key"})
    end, "Missing required field: scope%.key")
end)

test("optional field missing returns default", function()
    local r = validator.validate_value(nil, { type = "integer", default = 99 }, {"k"})
    assert_eq(r, 99)
end)

test("optional field missing with no default returns nil", function()
    local r = validator.validate_value(nil, { type = "string" }, {"k"})
    assert_eq(r, nil)
end)

test("below minimum raises error", function()
    assert_err(function()
        validator.validate_value(-1, { type = "integer", min = 0 }, {"k"})
    end, "below minimum")
end)

test("above maximum raises error", function()
    assert_err(function()
        validator.validate_value(70000, { type = "integer", max = 65535 }, {"k"})
    end, "above maximum")
end)

test("string instead of boolean raises error", function()
    assert_err(function()
        validator.validate_value("true", { type = "boolean" }, {"k"})
    end, "must be boolean")
end)

test("boolean instead of string raises error", function()
    assert_err(function()
        validator.validate_value(true, { type = "string" }, {"k"})
    end, "must be string")
end)

test("array passes with correct subtype", function()
    local r = validator.validate_value(
        {"a", "b", "c"}, { type = "array", subtype = "string" }, {"k"})
    assert_eq(#r, 3)
    assert_eq(r[1], "a")
end)

test("array item type mismatch raises error", function()
    assert_err(function()
        validator.validate_value({"a", 123}, { type = "array", subtype = "string" }, {"k"})
    end, "Array item.*must be string")
end)

-- ══════════════════════════════════════════════════════════════════════════
-- Group 2 — merge_schemas
-- ══════════════════════════════════════════════════════════════════════════

print("\n── Group 2: merge_schemas ──")

test("adds new scope from extra", function()
    local base  = { ufsrv = { server_id = { type = "integer" } } }
    local extra = { ufnet = { msgqueue = { type = "table" } } }
    local m = validator.merge_schemas(base, extra)
    assert_eq(m.ufsrv.server_id.type, "integer")
    assert_eq(m.ufnet.msgqueue.type, "table")
end)

test("deep-merges fields into existing scope", function()
    local base  = { ufsrv = { a = { type = "integer" } } }
    local extra = { ufsrv = { b = { type = "string" } } }
    local m = validator.merge_schemas(base, extra)
    assert_eq(m.ufsrv.a.type, "integer")
    assert_eq(m.ufsrv.b.type, "string")
end)

test("nil extra is no-op", function()
    local base = { ufsrv = { a = { type = "integer" } } }
    local m = validator.merge_schemas(base, nil)
    assert_eq(m.ufsrv.a.type, "integer")
end)

test("extra with empty scope merges cleanly", function()
    local base  = { ufsrv = { a = { type = "integer" } } }
    local extra = { ufsrvwebsock = {} }
    local m = validator.merge_schemas(base, extra)
    assert_eq(m.ufsrv.a.type, "integer")
    assert_eq(type(m.ufsrvwebsock), "table")
end)

test("extra overrides existing field", function()
    local base  = { ufsrv = { a = { type = "integer", default = 0 } } }
    local extra = { ufsrv = { a = { type = "integer", default = 99 } } }
    local m = validator.merge_schemas(base, extra)
    assert_eq(m.ufsrv.a.default, 99, "extra overrides default")
end)

-- ══════════════════════════════════════════════════════════════════════════
-- Group 3 — validate_config (full integration against real schemas)
-- ══════════════════════════════════════════════════════════════════════════

print("\n── Group 3: validate_config (ufsrv core schema) ──")

test("valid_full config passes", function()
    local cfg = dofile(fixture("valid_full.lua"))
    local r = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua",
        etc_dir .. "core_schema_ufnet.lua")
    assert_eq(r.ufsrv.main_listener_port, 19701)
    assert_eq(r.ufsrv.server_run_mode, "normal")
    assert_eq(r.ufnet.db_backend.port, 19800)
end)

test("defaults applied for missing optional fields", function()
    local cfg = dofile(fixture("valid_full.lua"))
    local r = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua", nil)
    assert_eq(r.ufsrv.server_cpu_affinity, "system_override",
              "server_cpu_affinity default")
    assert_eq(r.ufsrv.buffer_sizes.outgoing_buffer_size, 1024,
              "outgoing_buffer_size default")
end)

test("missing required field raises error", function()
    assert_err(function()
        local cfg = dofile(fixture("missing_required.lua"))
        validator.validate_config(cfg, etc_dir .. "core_schema_ufsrv.lua", nil)
    end, "Missing required field: ufsrv%.ufsrv_geogroup")
end)

test("type mismatch raises error", function()
    assert_err(function()
        local cfg = dofile(fixture("type_mismatch.lua"))
        validator.validate_config(cfg, etc_dir .. "core_schema_ufsrv.lua", nil)
    end, "must be integer")
end)

test("out of bounds raises error", function()
    assert_err(function()
        local cfg = dofile(fixture("out_of_bounds.lua"))
        validator.validate_config(cfg, etc_dir .. "core_schema_ufsrv.lua", nil)
    end, "below minimum")
end)

test("empty config raises missing required", function()
    assert_err(function()
        local cfg = dofile(fixture("empty.lua"))
        validator.validate_config(cfg, etc_dir .. "core_schema_ufsrv.lua", nil)
    end, "Missing required")
end)

test("unknown scope silently ignored", function()
    local cfg = dofile(fixture("unknown_scope.lua"))
    local r = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua", nil)
    assert_eq(r.ufsrv.main_listener_port, 19701)
    -- some_random_scope is not in the schema, so it's never validated
end)

test("missing required nested field raises error", function()
    assert_err(function()
        local cfg = { ufnet = { db_backend = { address = "h", username = "u" } } }
        validator.validate_config(cfg, etc_dir .. "core_schema_ufnet.lua", nil)
    end, "Missing required field: ufnet%.db_backend%.port")
end)

-- ══════════════════════════════════════════════════════════════════════════
-- Group 4 — validate_config (adjunct schemas)
-- ══════════════════════════════════════════════════════════════════════════

print("\n── Group 4: validate_config (adjunct schemas) ──")

test("adjunct-only validation passes", function()
    local cfg = dofile(fixture("adjunct_only.lua"))
    local r = validator.validate_config(cfg, nil,
        etc_dir .. "adjunct_schema_ufsrvapi.lua")
    assert_eq(r.ufsrvapi.this_server_origin, "https://api.example.com")
    assert_eq(r.ufsrvapi.cache_authorisation.size, 2048)
end)

test("adjunct empty scope is a no-op", function()
    local cfg = { ufsrv = { ufsrv_geogroup = 1, main_listener_port = 19701 } }
    local r = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua",
        etc_dir .. "adjunct_schema_ufsrvwebsock.lua")
    assert_eq(r.ufsrv.main_listener_port, 19701)
end)

test("adjunct empty string treated as nil", function()
    local cfg = { ufsrv = { ufsrv_geogroup = 1, main_listener_port = 19701 } }
    local r = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua", "")
    assert_eq(r.ufsrv.main_listener_port, 19701)
end)

test("three-tier: ufsrv + ufnet + adjunct merge", function()
    -- First validate ufsrv + ufnet
    local cfg = dofile(fixture("valid_full.lua"))
    local r1 = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua",
        etc_dir .. "core_schema_ufnet.lua")
    assert_eq(r1.ufsrv.server_id, 1)
    assert_eq(r1.ufnet.db_backend.port, 19800)

    -- Then add api adjunct
    cfg.ufsrvapi = {
        this_server_origin = "https://api.example.com",
        ufsrv_media_upload_uri = "https://media.example.com/",
    }
    local r2 = validator.validate_config(cfg,
        nil, etc_dir .. "adjunct_schema_ufsrvapi.lua")
    assert_eq(r2.ufsrvapi.this_server_origin, "https://api.example.com")
    -- defaults still applied
    assert_eq(r2.ufsrvapi.cache_authorisation.size, 1024)
end)

test("core + adjunct both nil raises error", function()
    assert_err(function()
        validator.validate_config({}, nil, nil)
    end, "at least one schema")
end)

test("adjunct schema is loaded and merged only if not empty", function()
    local cfg = { ufsrv = { ufsrv_geogroup = 1, main_listener_port = 19701 } }
    -- Pass empty string for adjunct — should work same as nil
    local r = validator.validate_config(cfg,
        etc_dir .. "core_schema_ufsrv.lua", "")
    assert_eq(r.ufsrv.server_cpu_affinity, "system_override")
end)

-- ══════════════════════════════════════════════════════════════════════════
-- Group 5 — sPathAppend / nested path correctness
-- ══════════════════════════════════════════════════════════════════════════

print("\n── Group 5: nested path correctness ──")

test("nested path includes all levels", function()
    -- db_backend.port missing → path should be ufnet.db_backend.port
    assert_err(function()
        local cfg = { ufnet = { db_backend = { address = "h", username = "u" } } }
        validator.validate_config(cfg, etc_dir .. "core_schema_ufnet.lua", nil)
    end, "ufnet%.db_backend%.port")
end)

test("doubly-nested path includes all levels", function()
    -- user_timeouts.unauthenticated_timeout is optional, but if we make it
    -- integer and pass a string, we get the full path
    assert_err(function()
        local cfg = {
            ufsrv = {
                ufsrv_geogroup = 1,
                main_listener_port = 19701,
                user_timeouts = { unauthenticated_timeout = "not-a-number" }
            }
        }
        validator.validate_config(cfg, etc_dir .. "core_schema_ufsrv.lua", nil)
    end, "ufsrv%.user_timeouts%.unauthenticated_timeout")
end)

-- ══════════════════════════════════════════════════════════════════════════
-- Results
-- ══════════════════════════════════════════════════════════════════════════

print("\n═══════════════════════════════════════════")
print(string.format("  %d passed, %d failed", passed, failed))
print("═══════════════════════════════════════════\n")

if failed > 0 then
    os.exit(1)
end
