-- Run with: reaper -newinst -nosplash scripts/host-tests/reaper-smoke.lua
-- The Agentic Dexed VST3 path must already be present in REAPER's VST3 paths.
local separator = package.config:sub(1, 1)
local script_path = debug.getinfo(1, "S").source:gsub("^@", "")
local script_dir = assert(script_path:match("^(.*[\\/])"))
local output_dir = script_dir .. ".." .. separator .. ".." .. separator .. "build" .. separator .. "host-tests"
reaper.RecursiveCreateDirectory(output_dir, 0)

local result_path = output_dir .. separator .. "reaper-smoke-result.txt"
local project_path = output_dir .. separator .. "Agentic-Dexed-REAPER-Smoke.rpp"
local render_dir = output_dir .. separator .. "render"
reaper.RecursiveCreateDirectory(render_dir, 0)

local lines = {}
local failures = 0

local function record(name, passed, detail)
  if not passed then failures = failures + 1 end
  lines[#lines + 1] = string.format("%s\t%s\t%s", passed and "PASS" or "FAIL", name, tostring(detail or ""))
end

local function finish()
  local file = assert(io.open(result_path, "w"))
  file:write("REAPER_VERSION\t", reaper.GetAppVersion(), "\n")
  file:write("FAILURES\t", failures, "\n")
  for _, line in ipairs(lines) do file:write(line, "\n") end
  file:close()
end

local function add_agentic_dexed(track)
  local names = {
    "VST3i: Agentic Dexed (Agentic Dexed)",
    "VST3: Agentic Dexed (Agentic Dexed)",
    "Agentic Dexed (Agentic Dexed)",
    "Agentic Dexed"
  }
  for _, name in ipairs(names) do
    local fx = reaper.TrackFX_AddByName(track, name, false, 1)
    if fx >= 0 then return fx, name end
  end
  return -1, table.concat(names, " | ")
end

reaper.PreventUIRefresh(1)
reaper.Undo_BeginBlock()
for index = reaper.CountTracks(0) - 1, 0, -1 do
  reaper.DeleteTrack(reaper.GetTrack(0, index))
end

reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
record("track-created", track ~= nil, "track 1")

local fx, requested_name = add_agentic_dexed(track)
record("plugin-scan-and-load", fx >= 0, requested_name)
if fx < 0 then
  reaper.Undo_EndBlock("Agentic Dexed REAPER smoke", -1)
  reaper.PreventUIRefresh(-1)
  finish()
  return
end

local _, fx_name = reaper.TrackFX_GetFXName(track, fx, "")
local parameter_count = reaper.TrackFX_GetNumParams(track, fx)
record("plugin-identity", fx_name:find("Agentic Dexed", 1, true) ~= nil, fx_name)
record("parameter-discovery", parameter_count > 0, parameter_count)

reaper.TrackFX_Show(track, fx, 3)
record("editor-open", reaper.TrackFX_GetOpen(track, fx), "floating editor")
reaper.TrackFX_Show(track, fx, 2)
record("editor-close", not reaper.TrackFX_GetOpen(track, fx), "floating editor hidden")

local fuzz_ok = true
local fuzzed = math.min(parameter_count, 48)
local saved_values = {}
for index = 0, fuzzed - 1 do
  local value = ((index * 37) % 101) / 100.0
  reaper.TrackFX_SetParamNormalized(track, fx, index, value)
  local actual = reaper.TrackFX_GetParamNormalized(track, fx, index)
  saved_values[index] = actual
  if actual ~= actual or actual < 0.0 or actual > 1.0 then fuzz_ok = false end
end
record("parameter-fuzz", fuzz_ok and fuzzed > 0, fuzzed)

local serialized_values = {}
for index = 0, fuzzed - 1 do
  serialized_values[#serialized_values + 1] = string.format("%.17g", saved_values[index])
end
record("saved-parameter-state", fuzzed > 0, table.concat(serialized_values, ","))

local midi_item = reaper.CreateNewMIDIItemInProj(track, 0.0, 2.0, false)
local take = midi_item and reaper.GetActiveTake(midi_item) or nil
local inserted = false
if take then
  inserted = reaper.MIDI_InsertNote(take, false, false, 0, 1440, 0, 60, 108, false)
  reaper.MIDI_InsertCC(take, false, false, 0, 0xB0, 0, 1, 96)
  reaper.MIDI_Sort(take)
end
record("midi-item-and-note", inserted, "C4 velocity 108 plus CC1")

local cycles_ok = true
for _ = 1, 50 do
  reaper.InsertTrackAtIndex(1, false)
  local cycle_track = reaper.GetTrack(0, 1)
  if add_agentic_dexed(cycle_track) < 0 then cycles_ok = false end
  reaper.DeleteTrack(cycle_track)
end
record("repeated-load-unload", cycles_ok, "50 cycles")

reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", render_dir, true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "agentic-dexed-smoke", true)
reaper.Main_SaveProjectEx(0, project_path, 0)
record("project-save", reaper.file_exists(project_path), project_path)

local chunk_ok, track_chunk = reaper.GetTrackStateChunk(track, "", false)
local restore_ok = chunk_ok
if chunk_ok and fuzzed > 0 then
  reaper.TrackFX_SetParamNormalized(track, fx, 0, saved_values[0] > 0.5 and 0.0 or 1.0)
  restore_ok = reaper.SetTrackStateChunk(track, track_chunk, false)
  local restored = reaper.TrackFX_GetParamNormalized(track, fx, 0)
  restore_ok = restore_ok and math.abs(restored - saved_values[0]) < 0.000001
end
record("fx-state-restore", restore_ok, "exact track state chunk round trip")

reaper.Undo_EndBlock("Agentic Dexed REAPER smoke", -1)
reaper.PreventUIRefresh(-1)
finish()
reaper.UpdateArrange()
