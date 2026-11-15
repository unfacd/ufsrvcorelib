#!/usr/bin/env lua
-- test_truth_validator.lua — validates a truth-table dump against the original
-- Lua config file.  Called by the adversarial C++ test before the truth table
-- is used for in-memory comparisons.
--
-- Usage: lua test_truth_validator.lua <config.lua> <truth_dump.txt>
--
-- Truth dump format (one line per entry):
--   ABSENT scope.key
--   STR scope.key value
--   INT scope.key value
--   BOOL scope.key value
--   ARRAY scope.key count elem1 elem2 ...
--   DOUBLE scope.key value
--   SKIP scope.key
--
-- Exits 0 if all entries match, non-zero on any discrepancy.

local function trim(s)
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function split(str)
  local t = {}
  for token in str:gmatch("%S+") do
    t[#t + 1] = token
  end
  return t
end

-- Resolve a dot-path key like "ufsrv.buffer_sizes.incoming_buffer_size"
-- in the Lua global table.  Returns the value and its Lua type string,
-- or nil,"ABSENT" if not found.
local function lookup(scope, key)
  local parts = {}
  for part in (scope .. "." .. key):gmatch("[^.]+") do
    parts[#parts + 1] = part
  end

  local t = _G
  for i = 1, #parts do
    if type(t) ~= "table" then
      return nil, "ABSENT"
    end
    t = t[parts[i]]
    if t == nil then
      return nil, "ABSENT"
    end
  end

  return t, type(t)
end

-- Load and execute the config file (populates _G)
local config_file = arg[1]
local truth_file = arg[2]

if not config_file or not truth_file then
  io.stderr:write("Usage: lua test_truth_validator.lua <config.lua> <truth.txt>\n")
  os.exit(1)
end

local config_chunk, err = loadfile(config_file)
if not config_chunk then
  io.stderr:write("FAIL: cannot load " .. config_file .. ": " .. err .. "\n")
  os.exit(1)
end
config_chunk()

-- Read truth table and verify each entry
local fh = io.open(truth_file, "r")
if not fh then
  io.stderr:write("FAIL: cannot open " .. truth_file .. "\n")
  os.exit(1)
end

local errors = 0
local checked = 0
local line_no = 0

for line in fh:lines() do
  line_no = line_no + 1
  line = trim(line)
  if line == "" then goto continue end
  -- Skip comment lines
  if line:sub(1, 1) == "#" then goto continue end

  local tokens = split(line)
  local tag = tokens[1]
  local full_key = tokens[2]

  if not full_key then
    io.stderr:write("PARSE ERROR line " .. line_no .. ": " .. line .. "\n")
    errors = errors + 1
    goto continue
  end

  -- Parse scope.key
  local dot = full_key:find("%.")
  if not dot then
    io.stderr:write("PARSE ERROR line " .. line_no .. ": no dot in key\n")
    errors = errors + 1
    goto continue
  end
  local scope = full_key:sub(1, dot - 1)
  local key   = full_key:sub(dot + 1)

  local val, vtype = lookup(scope, key)

  if tag == "ABSENT" then
    if vtype ~= "ABSENT" then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected ABSENT but found " .. vtype .. "\n")
      errors = errors + 1
    end
    checked = checked + 1
  elseif tag == "SKIP" then
    -- Skip entries with unsupported types (PTR, STRUCT, etc.)
    checked = checked + 1
  elseif tag == "STR" then
    local expected = tokens[3] or ""
    if vtype ~= "string" then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected STR but got " .. vtype .. "\n")
      errors = errors + 1
    elseif tostring(val) ~= expected then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected '" .. expected .. "' got '" .. tostring(val) .. "'\n")
      errors = errors + 1
    end
    checked = checked + 1
  elseif tag == "INT" then
    local expected = tonumber(tokens[3])
    if vtype ~= "number" then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected INT but got " .. vtype .. "\n")
      errors = errors + 1
    elseif val ~= expected then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected " .. expected .. " got " .. tostring(val) .. "\n")
      errors = errors + 1
    end
    checked = checked + 1
  elseif tag == "BOOL" then
    local expected = tonumber(tokens[3])
    if vtype ~= "boolean" then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected BOOL but got " .. vtype .. "\n")
      errors = errors + 1
    else
      local as_int = val and 1 or 0
      if as_int ~= expected then
        io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
          .. " expected " .. expected .. " got " .. as_int .. "\n")
        errors = errors + 1
      end
    end
    checked = checked + 1
  elseif tag == "DOUBLE" then
    local expected = tonumber(tokens[3])
    if vtype ~= "number" then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected DOUBLE but got " .. vtype .. "\n")
      errors = errors + 1
    elseif math.abs(val - expected) > 0.0001 then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected ~" .. expected .. " got " .. tostring(val) .. "\n")
      errors = errors + 1
    end
    checked = checked + 1
  elseif tag == "ARRAY" then
    local count = tonumber(tokens[3]) or 0
    if vtype ~= "table" then
      io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
        .. " expected ARRAY but got " .. vtype .. "\n")
      errors = errors + 1
    else
      local actual_len = #val
      if actual_len ~= count then
        io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
          .. " expected " .. count .. " elements, got " .. actual_len .. "\n")
        errors = errors + 1
      else
        for i = 1, count do
          local expected_elem = tokens[3 + i]
          local actual_elem = val[i]
          if tostring(actual_elem) ~= expected_elem then
            io.stderr:write("MISMATCH line " .. line_no .. ": " .. full_key
              .. "[" .. i .. "] expected '" .. (expected_elem or "nil")
              .. "' got '" .. tostring(actual_elem) .. "'\n")
            errors = errors + 1
          end
        end
      end
    end
    checked = checked + 1
  else
    io.stderr:write("PARSE ERROR line " .. line_no .. ": unknown tag '" .. tag .. "'\n")
    errors = errors + 1
  end

  ::continue::
end

fh:close()

if errors > 0 then
  io.stderr:write("VALIDATION FAILED: " .. errors .. " error(s), "
    .. checked .. " entries checked\n")
  os.exit(1)
end

io.stderr:write("TRUTH TABLE VALID: " .. checked .. " entries match\n")
os.exit(0)
