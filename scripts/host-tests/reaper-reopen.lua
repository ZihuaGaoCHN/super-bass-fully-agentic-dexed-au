-- Run after reaper-smoke.lua with the saved .rpp followed by this script.
local separator = package.config:sub(1, 1)
local script_path = debug.getinfo(1, "S").source:gsub("^@", "")
local script_dir = assert(script_path:match("^(.*[\\/])"))
local output_dir = script_dir .. ".." .. separator .. ".." .. separator .. "build" .. separator .. "host-tests"
local first_result = output_dir .. separator .. "reaper-smoke-result.txt"
local result_path = output_dir .. separator .. "reaper-reopen-result.txt"

local lines = {}
local failures = 0
local function record(name, passed, detail)
  if not passed then failures = failures + 1 end
  lines[#lines + 1] = string.format("%s\t%s\t%s", passed and "PASS" or "FAIL", name, tostring(detail or ""))
end

local expected = {}
for line in io.lines(first_result) do
  local values = line:match("^PASS\tsaved%-parameter%-state\t(.+)$")
  if values then
    for value in values:gmatch("[^,]+") do expected[#expected + 1] = tonumber(value) end
  end
end

local track = reaper.GetTrack(0, 0)
record("saved-track-reopened", track ~= nil, reaper.GetProjectName(0, ""))
local fx_count = track and reaper.TrackFX_GetCount(track) or 0
record("saved-plugin-reopened", fx_count == 1, fx_count)
if track and fx_count == 1 then
  local _, fx_name = reaper.TrackFX_GetFXName(track, 0, "")
  record("saved-plugin-identity", fx_name:find("Agentic Dexed", 1, true) ~= nil, fx_name)

  local exact = #expected == 48
  local largest_delta = 0.0
  for index = 0, #expected - 1 do
    local actual = reaper.TrackFX_GetParamNormalized(track, 0, index)
    local delta = math.abs(actual - expected[index + 1])
    if delta > largest_delta then largest_delta = delta end
    if delta > 0.000001 then exact = false end
  end
  record("saved-parameter-state-reopened", exact,
         string.format("%d values, max delta %.17g", #expected, largest_delta))

  reaper.TrackFX_Show(track, 0, 3)
  record("reopened-editor-open", reaper.TrackFX_GetOpen(track, 0), "floating editor")
  reaper.TrackFX_Show(track, 0, 2)
  record("reopened-editor-close", not reaper.TrackFX_GetOpen(track, 0), "floating editor hidden")

  local item = reaper.GetTrackMediaItem(track, 0)
  local take = item and reaper.GetActiveTake(item) or nil
  local note_count = take and reaper.MIDI_CountEvts(take) or 0
  record("saved-midi-reopened", note_count >= 1, note_count)
end

local file = assert(io.open(result_path, "w"))
file:write("REAPER_VERSION\t", reaper.GetAppVersion(), "\n")
file:write("FAILURES\t", failures, "\n")
for _, line in ipairs(lines) do file:write(line, "\n") end
file:close()
