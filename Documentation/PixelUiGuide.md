# Super Bass Fully Agentic Dexed pixel UI guide

The editor uses a 1280 × 760 reference canvas and a 960 × 640 minimum. At widths below 1200 px, the top bar uses two rows and the synth workspace shows one selected Operator card. The Agent panel and performance keyboard collapse independently. The supported scale choices are 100%, 125%, 150%, and 200%.

## Visual language

- Deep blue is the canvas and panel surface.
- Cyan marks normal focus and active synthesis state.
- Amber marks Agent-authored changes and proposals. Every amber state also includes `AI`, `REVIEW PROPOSAL`, or another text marker.
- Red is reserved for errors and clipping. Error states include a readable code and bounded message.
- Controls, borders, and spacing follow the 8 px grid. Noto Sans supplies deterministic Latin text; the platform fallback handles CJK and emoji.

## Accessibility

Top-level commands expose names, titles, JUCE accessibility roles, and explicit focus order. Keyboard users can reach program selection, every legacy workflow, Agent and keyboard toggles, settings, Reduced Motion, and scale. Reduced Motion sets optional transition time to zero. Text/background palette pairs meet WCAG AA contrast.

## Render maintenance

`UiRenderTests` renders minimum, reference, and 150% layouts plus streaming, proposed, conflict, missing-key, network-error, and clipping states. PNG review artifacts are written under `build/<platform>/ui-render`; they are build outputs so font rasterization stays platform-specific. The checked manifests in `Tests/golden/windows` and `Tests/golden/macos` define the required geometry and scenarios.

Review PNGs at original resolution. Check clipped labels, fuzzy one-pixel borders, inconsistent 8 px spacing, missing state text, CJK/emoji fallback, Operator selection, algorithm carriers, and the red clipping meter. Update product code when geometry changes; update a platform manifest only when the intended reference layout changes.

Run the gate with:

```sh
cmake --build build/windows --config Release --target AgenticDexedTests AgenticDexed_VST3 AgenticDexed_Standalone
ctest --test-dir build/windows -C Release --output-on-failure
```

The event-budget test delivers 1,000 stream updates in one batch. The Agent panel must apply no more than 35 UI updates; the current last-value coalescer applies one.
