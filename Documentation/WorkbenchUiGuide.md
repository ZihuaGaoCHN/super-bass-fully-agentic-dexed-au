# Super Bass Fully Agentic Dexed Workbench UI Guide

The native VST3 and standalone application use one workbench shell on macOS and
Windows. The visual language is a compact cream, ink, and blue instrument panel.
Every editor view lives inside the same header, page tabs, content host, and
status bar.

## Shell and navigation

The top mode switch selects **SYNTH** or **SYSTEM**. The patch header underneath
contains patch navigation, import/save actions, engine information, and the
output meter. In SYNTH mode, the numbered tabs open:

1. **Sound** — operators, algorithm, feedback, envelopes, pitch, and global voice controls.
2. **Modulation** — performance sources, routing matrix, and modulation ranges.
3. **Effects** — filter, output, engine character, and global processing controls.
4. **Presets** — searchable cartridge and patch browsing, import, save, and corruption states.
5. **Generate** — bilingual Agent conversation, change review, history, and model settings.

SYSTEM mode opens MIDI, audio/runtime, tuning, diagnostics, and about information.
The optional keyboard remains docked below the current page. The bottom status
bar always reports the current state in text as well as colour.

Use `Ctrl+1` through `Ctrl+5` on Windows or `Command+1` through `Command+5` on
macOS to switch SYNTH pages. Press `Escape` to close the active overlay.

## Agent input and credentials

The Agent prompt supports Chinese, English, emoji, and multiple lines. During an
IME composition, `Enter` remains available to choose or commit the candidate.
After composition is complete, use `Ctrl+Enter` on Windows or `Command+Enter` on
macOS to submit the prompt.

Open **Model Settings** from the Generate page to choose the protocol, provider
URL, model, and apply mode. Paste a new API key into the credential field, then
choose **Apply** or **Test Connection**. A stored key is shown only as a mask and
is never read back into the editor. Choose **Replace Key** to enter a new value or
**Forget Key** to remove it. If secure storage is unavailable, the UI clearly
marks the key as available to the current process only.

## Responsive layout

The reference layout is 1280 x 760 with a minimum logical canvas of 960 x 760.
On Windows, resizing scales the complete workbench (text, controls, curves, and
hit areas) together. Windows normally permits windows down to 640 x 480; the
minimum and restored size also fit smaller available desktop areas, leaving
space for the title bar and taskbar. JUCE handles monitor DPI separately, so
the workbench does not apply the monitor's DPI factor twice. Actual window
dimensions are saved separately from the scaled logical canvas.

macOS keeps its existing window sizing. Both platforms show taller operator
and pitch envelopes beside their parameter controls, including with the
keyboard expanded. Parameter pages fit without whole-page scrolling.
The sizing suite covers six window sizes and desktop work areas corresponding
to 100%, 125%, 150%, and 200% DPI. Actual multi-monitor hardware behavior still
requires native validation on the relevant displays.

All enabled interactive controls expose a readable name and keyboard focus.
Normal text meets a 4.5:1 contrast target. Warning and error surfaces carry text
labels in addition to colour, and their non-text boundaries meet a 3:1 target.
Reduced-motion mode uses immediate state changes.

## Visual verification

The platform evidence manifests are:

- `Tests/golden/windows/workbench-manifest.json`
- `Tests/golden/macos/workbench-manifest.json`

Run the focused render suite from a configured build:

```text
AgenticDexedTests --filter WorkbenchRender
```

It writes per-page breakpoint images, scale images, explicit state images, and a
contact sheet to the configured `workbench-render` output directory. Inspect the
contact sheet after any layout, font, colour, or platform-renderer change. The
accessibility and update-budget companions are `WorkbenchAccessibility` and
`WorkbenchUpdateBudget`.

Before a release, manually verify Microsoft Pinyin on Windows and Pinyin input
on macOS in both the standalone application and a VST3 host. Confirm that IME
candidate selection does not submit, the committed Chinese text remains intact,
and the platform submit shortcut sends exactly once.
