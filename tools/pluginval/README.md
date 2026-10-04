# pluginval release pin

Agentic Dexed validates release VST3 bundles with Tracktion pluginval `v1.0.4`.
The download scripts fetch the platform archive from the versioned upstream
GitHub release and cache it below `tools/pluginval/cache/<version>/<platform>`.
The cache and validation logs are build outputs and are not committed.

The validation wrappers deliberately run pluginval out of process. They use
sample rates 44100, 48000, and 96000 Hz and block sizes 1, 32, 64, 512, and
1024. CI uses strictness 5; signed release candidates use strictness 8.

Upstream: https://github.com/Tracktion/pluginval/releases/tag/v1.0.4

