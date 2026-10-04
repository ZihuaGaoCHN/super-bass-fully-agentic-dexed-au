# REAPER 7 release smoke test

Use this checklist for each signed release candidate. Run it on a clean user
account with no development-tree VST path configured. A required row is complete
only when its result, evidence location, package checksum, OS build, architecture,
and REAPER version are recorded in `Documentation/host-tests/results/`.

The Windows ReaScripts in `scripts/host-tests/` automate the repeatable host
checks. They supplement the interactive checks; they do not replace credential,
network, accessibility, or installer checks.

## Candidate setup

1. Verify the published SHA-256 checksum and platform signature before install.
2. Install the VST3 with the release installer into the platform's common VST3
   directory. Do not add a development build directory to REAPER.
3. Start a fresh REAPER 7 profile, clear the VST cache, rescan, and record the
   scan log. Confirm the entry is `VST3i: Agentic Dexed (Agentic Dexed)` and that
   original Dexed can remain installed without a collision.
4. Create a two-bar MIDI item containing a held note, velocity variation, pitch
   bend, mod wheel, sustain, and aftertouch. Route it only to Agentic Dexed.
5. Save evidence outside the installed application directory. Do not include an
   API key, authorization header, prompt text, or model response body.

## Common Windows and macOS checks

| ID | Required action | Passing result |
| --- | --- | --- |
| PKG-01 | Verify checksum, signature, product/version, plugin code, and bundle ID. | Checksum matches; signature is valid; product is 1.0.1; code is `AgDx`; bundle ID is `com.agenticdexed.AgenticDexed`. |
| INS-01 | Install to the common VST3 directory as a standard user, then launch REAPER. | Install completes without copying files from the development tree. |
| HOST-01 | Clear cache, rescan, insert the instrument, open and close its editor. | One Agentic Dexed entry appears; insertion and editor lifecycle succeed without a scan error or crash. |
| MIDI-01 | Play the prepared MIDI item and render it offline. | All notes terminate; output is finite, non-silent, and free of stuck notes. |
| SYN-01 | Select Modern, Mark I, and OPL engines and play/render each. | All three engines produce finite audio and the selection survives project reopen. |
| SYN-02 | Browse programs, load a cartridge, store a program, close, and reopen. | Program name, cartridge selection, and stored voice are preserved. |
| SYN-03 | Import a DX7 voice and cartridge SysEx, export both, and load the exports in a new instance. | Compatible content round-trips and checksums are accepted. |
| TUNE-01 | Load SCL alone, SCL with KBM, restore standard tuning, then reopen. | Tuning changes are audible/preserved and standard tuning is restored on command. |
| MIDI-02 | Learn a MIDI CC for a parameter, exercise it, remove the mapping, and reopen. | The mapping controls only the intended parameter and removal persists. |
| AUTO-01 | Write automation for cutoff, algorithm, and an operator level; play it back and reopen. | Host lanes follow parameter changes, playback is deterministic, and values restore. |
| STATE-01 | Save, close, and reopen the REAPER project after edits. | Synth, engine, tuning, program, UI preferences, and automatable values restore; Agent secrets/conversation do not enter the project. |
| UI-01 | Open SOUND, MODULATION, EFFECTS, PRESETS, GENERATE, and SYSTEM at 960×640, 1280×760, and a large window; test 100/125/150/200% scale and keyboard navigation. | Every page remains reachable; contained scrolling preserves actions; focus is visible; bilingual text is readable; no legacy UI appears. |
| UI-02 | In GENERATE, compose a multiline Chinese prompt with the platform IME, use candidate selection and Backspace, then submit with Ctrl/Command+Enter. | Composition Enter does not submit; committed Chinese text is intact; the shortcut submits exactly once; status remains meaningful without colour. |
| CRED-01 | Store, replace, test, and forget an API key; inspect the project and normal preferences. | The key uses Credential Manager/Keychain, is never echoed in full, and is absent after forget. |
| NET-01 | Test an OpenAI Responses connection and run one request. | Streaming/tool activity completes and the request uses stateless storage behavior. |
| NET-02 | Test an OpenAI-compatible Chat Completions endpoint, including a loopback HTTP endpoint. | HTTPS remote and loopback HTTP behave as documented; unsafe remote HTTP is rejected. |
| AGENT-01 | In live mode, request a patch creation and a refinement. | Valid transactions apply immediately, show a diff/reason, and stop within four rounds. |
| AGENT-02 | Repeat in confirmation mode, reject once, then accept. | The synth remains unchanged before acceptance and applies the complete proposal once. |
| AGENT-03 | Cancel during streaming and during audition analysis. | The request stops promptly; no incomplete tool call or late edit is applied. |
| TX-01 | Undo and redo an Agent transaction. | The complete change set round-trips exactly as one transaction. |
| TX-02 | Start an Agent edit, manually change an overlapping parameter, then let the Agent finish. | A visible conflict is reported and the manual value is not silently overwritten. |
| AUD-01 | Run offline audition for normal, silent, and clipped patches. | Metrics are finite; silence/clipping are flagged; raw audio or MIDI is not sent to the provider. |
| LIFE-01 | Close the editor while a request is active, reopen it, then remove the plug-in. | Work is cancelled safely and REAPER remains responsive with no late callback. |
| LIFE-02 | Add/remove the plug-in 50 times and reopen the saved project. | Every load/unload succeeds and restored parameter values match exactly. |
| UN-01 | Uninstall, rescan REAPER, and inspect the common VST3 directory. | Agentic Dexed disappears and no installed product file remains. User-created patches remain. |

## ReaScript-assisted host checks

With the candidate installed and visible to REAPER, run:

```text
reaper -newinst -nosplash scripts/host-tests/reaper-smoke.lua
reaper -newinst -nosplash build/host-tests/Agentic-Dexed-REAPER-Smoke.rpp scripts/host-tests/reaper-reopen.lua
reaper -newinst -nosplash -renderproject build/host-tests/Agentic-Dexed-REAPER-Smoke.rpp
```

Require `FAILURES<TAB>0` in both result files. The first script performs scan/load,
editor open/close, 48-parameter fuzz, MIDI creation, 50 load/unload cycles, project
save, and an exact state-chunk round trip. The second proves exact parameter and
MIDI restoration after a new REAPER process loads the saved project. Inspect the
rendered WAV for format, finite samples, peak, RMS, and non-silence.

## macOS additions

Run the full common checklist natively on Apple Silicon in both Standalone and
VST3, and confirm both formats expose the same six workbench pages. Repeat the
host, state, automation, and lifecycle rows in an x86_64-capable validation environment.
Record `lipo -archs` for the VST3 executable. On a clean Mac, download the public
package in a browser, open it through Finder, and record Gatekeeper acceptance,
package notarization/stapling verification, and the Keychain create/replace/forget
prompts. Both architecture runs and Gatekeeper/Keychain checks are release gates.

## Result policy

Use `PASS`, `FAIL`, `NOT EXECUTED`, or `NOT APPLICABLE` for every row. `FAIL` and
`NOT EXECUTED` on a required row block public release. Developer-build evidence
must be labeled as preflight and cannot be used to claim a signed candidate passed.
