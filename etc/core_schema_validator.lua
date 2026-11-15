-- core_schema_validator.lua — Lua schema validator for ufsrv config files
--
-- Ships with ufsrvcorelib.  Provides validate_config() which loads a core
-- schema, optionally merges an adjunct schema, then validates every scope
-- in the config against the merged schema.
--
-- The caller (C code, or lua CLI) chains calls for multi-tier inheritance:
--   1. validate_config(config, "core_schema_ufsrv.lua", nil)
--   2. validate_config(config, nil, "adjunct_schema_ufsrvwebsock.lua")
--
-- Alternatively, the caller pre-merges all applicable schemas via
-- merge_schemas() and calls validate_config() once with the combined result.
--
-- Optional 4th argument `opts` (table):
--   opts.strict = true  → fail on any config key not present in the schema
--                         (blacklist / unknown-attribute validation).
--
-- Copyright (C) 2015-2026 unfacd works
-- SPDX-License-Identifier: AGPL-3.0-or-later

-- ── Path helper ─────────────────────────────────────────────────────────
--
-- In Lua 5.4 table.unpack() only expands fully as the *last* expression
-- in a table constructor.  When followed by another value, only the first
-- return is captured.  We build the path explicitly to avoid this.

local function sPathAppend(path, key)
    local newpath = {}
    for _, v in ipairs(path) do
        newpath[#newpath + 1] = v
    end
    newpath[#newpath + 1] = key
    return newpath
end

local function path_str(path)
    return table.concat(path, ".")
end

-- ── Deep copy (schema tables only; avoids mutating caller-supplied schemas)

local function deep_copy(t)
    if type(t) ~= "table" then
        return t
    end
    local c = {}
    for k, v in pairs(t) do
        c[k] = deep_copy(v)
    end
    return c
end

-- ── Is this a nested schema node (scope / sub-table)? ───────────────────
-- A schema node is a table that has no `.type` key (or has type=="table"
-- with an optional nested `.fields` / direct child field defs).

local function is_schema_node(def)
    return type(def) == "table" and (def.type == nil or def.type == "table")
end

-- Field definitions live either directly under the node, or under
-- def.fields when type == "table".
local function schema_children(def)
    if def.type == "table" and type(def.fields) == "table" then
        return def.fields
    end
    return def
end

-- ── Leaf value validation ────────────────────────────────────────────────

local function validate_value(value, def, path)
    -- Nil value: required fields error; optional fields use default
    if value == nil then
        if def.required then
            error(string.format("Missing required field: %s", path_str(path)))
        end
        return def.default
    end

    local vtype = type(value)

    -- Integer / number
    if def.type == "integer" or def.type == "number" then
        if vtype ~= "number" or (def.type == "integer" and value % 1 ~= 0) then
            error(string.format("Field %s must be %s, got %s",
                path_str(path), def.type, vtype))
        end
        if def.min and value < def.min then
            error(string.format("%s is below minimum (%s)",
                path_str(path), tostring(def.min)))
        end
        if def.max and value > def.max then
            error(string.format("%s is above maximum (%s)",
                path_str(path), tostring(def.max)))
        end

    -- String
    elseif def.type == "string" then
        if vtype ~= "string" then
            error(string.format("Field %s must be string, got %s",
                path_str(path), vtype))
        end

    -- Boolean
    elseif def.type == "boolean" then
        if vtype ~= "boolean" then
            error(string.format("Field %s must be boolean, got %s",
                path_str(path), vtype))
        end

    -- Array (1-indexed Lua sequence)
    elseif def.type == "array" then
        if vtype ~= "table" then
            error(string.format("Field %s must be array/table, got %s",
                path_str(path), vtype))
        end
        if def.subtype then
            local st = def.subtype
            for i, item in ipairs(value) do
                local it = type(item)
                if st == "string" and it ~= "string" then
                    error(string.format("Array item %s[%d] must be string, got %s",
                        path_str(path), i, it))
                elseif st == "table" and it ~= "table" then
                    error(string.format("Array item %s[%d] must be table, got %s",
                        path_str(path), i, it))
                elseif st == "boolean" and it ~= "boolean" then
                    error(string.format("Array item %s[%d] must be boolean, got %s",
                        path_str(path), i, it))
                elseif st == "integer" then
                    if it ~= "number" or item % 1 ~= 0 then
                        error(string.format("Array item %s[%d] must be integer, got %s",
                            path_str(path), i, it))
                    end
                elseif st == "number" then
                    if it ~= "number" then
                        error(string.format("Array item %s[%d] must be number, got %s",
                            path_str(path), i, it))
                    end
                end
            end
        end

    -- Explicit type = "table" without nested field schema: just type-check
    elseif def.type == "table" then
        if vtype ~= "table" then
            error(string.format("Field %s must be table, got %s",
                path_str(path), vtype))
        end
        -- Nested field validation is handled by validate_table when
        -- is_schema_node(def) is true (type == "table" with children).

    elseif def.type ~= nil then
        error(string.format("Field %s: unsupported schema type '%s'",
            path_str(path), tostring(def.type)))
    end

    return value
end

-- ── Table validation (recursive) ─────────────────────────────────────────

local function validate_table(tbl, schema_tbl, path, strict)
    path = path or {}
    local result = {}

    -- Build set of recognised field / sub-table keys (exclude boolean markers)
    local known = {}
    for key, def in pairs(schema_tbl) do
        if type(def) ~= "boolean" then
            known[key] = true
        end
    end

    -- Strict mode: reject any config key not in the schema
    if strict then
        for key, _ in pairs(tbl) do
            if not known[key] then
                local p = sPathAppend(path, key)
                error(string.format("Unrecognized attribute: %s", path_str(p)))
            end
        end
    end

    for key, def in pairs(schema_tbl) do
        if type(def) == "boolean" then
            -- Boolean marker (e.g. "required = true"): not a config field.
        elseif is_schema_node(def) then
            -- Sub-table / scope (no .type, or type == "table")
            local children = schema_children(def)
            local child = tbl[key]
            local child_path = sPathAppend(path, key)

            if child == nil then
                if def.required == true then
                    error(string.format("Missing required field: %s",
                        path_str(child_path)))
                end
                -- Optional sub-table absent → leave as nil (do not materialise)
            elseif type(child) ~= "table" then
                error(string.format("Field %s must be table, got %s",
                    path_str(child_path), type(child)))
            else
                result[key] = validate_table(child, children, child_path, strict)
            end
        else
            -- Leaf field (has a .type other than nested schema handling)
            local value = tbl[key]
            result[key] = validate_value(value, def, sPathAppend(path, key))
        end
    end

    return result
end

-- ── Schema merge (deep, non-mutating) ────────────────────────────────────
--
-- Returns a new table.  Neither `base` nor `extra` is modified.
-- Nested schema nodes (tables without .type, or type=="table") are
-- merged recursively so adjuncts can extend existing sub-tables without
-- wiping prior field definitions.

local function merge_schemas(base, extra)
    if not extra then
        return deep_copy(base)
    end
    if not base then
        return deep_copy(extra)
    end

    local result = deep_copy(base)

    for k, v in pairs(extra) do
        if result[k] ~= nil
           and type(result[k]) == "table"
           and type(v) == "table"
           and is_schema_node(result[k])
           and is_schema_node(v) then
            -- Recursively deep-merge nested schema nodes
            result[k] = merge_schemas(result[k], v)
        else
            -- New key, or leaf / incompatible types → copy from extra
            result[k] = deep_copy(v)
        end
    end

    return result
end

-- ── Top-level entry point ────────────────────────────────────────────────
--
-- opts (optional table):
--   strict = true  → reject unrecognised config attributes

local function validate_config(config, core_schema_file, adjunct_schema_file, opts)
    if not core_schema_file and not adjunct_schema_file then
        error("validate_config: at least one schema file is required")
    end

    opts = opts or {}
    local strict = opts.strict == true

    -- Load and merge schemas into a fresh table (never mutate loaded modules)
    local schema = {}

    if core_schema_file and core_schema_file ~= "" then
        local core = dofile(core_schema_file)
        schema = merge_schemas(schema, core)
    end

    if adjunct_schema_file and adjunct_schema_file ~= "" then
        local adjunct = dofile(adjunct_schema_file)
        schema = merge_schemas(schema, adjunct)
    end

    -- Validate config against the merged schema.
    return validate_table(config, schema, {}, strict)
end

-- ── Module exports ───────────────────────────────────────────────────────

return {
    validate_value  = validate_value,
    validate_table  = validate_table,
    merge_schemas   = merge_schemas,
    validate_config = validate_config,
    deep_copy       = deep_copy,   -- exposed for tests / callers
}
