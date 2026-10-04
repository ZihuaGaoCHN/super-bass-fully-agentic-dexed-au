# Release validation

The current publication candidate is not yet validated on both native platforms.
Windows local regression: 20,022 assertions; pluginval strictness 8; real-provider rollback: 403 assertions. Native macOS regression remains required. Historical reports are not used as proof for this candidate.

The build workflow runs the full CTest suite and VST3 pluginval at strictness 8 on Windows x64 and macOS arm64. UI geometry checks do not replace native IME or DAW manual acceptance.
