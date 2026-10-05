#!/bin/zsh
cd -- "${0:A:h}" || exit 1
report="Agent-check-$(date +%Y%m%d-%H%M%S).txt"
printf 'Super Bass Fully Agentic Dexed native ARM check, candidate 20261004-r4\n' | tee "$report"
if [[ "$(uname -m)" != "arm64" ]]; then
    printf 'Please launch Terminal natively on Apple Silicon, without Rosetta.\n' | tee -a "$report"
    exit 1
fi
sw_vers | tee -a "$report"
shasum -a 256 ./AgenticDexedTests './Super Bass Fully Agentic Dexed.app/Contents/MacOS/Super Bass Fully Agentic Dexed' './Super Bass Fully Agentic Dexed.vst3/Contents/MacOS/Super Bass Fully Agentic Dexed' | tee -a "$report"
printf 'Uses your saved DeepSeek key and makes real API calls. It creates a temporary patch; your open app and saved presets are not changed.\n' | tee -a "$report"
./AgenticDexedTests --filter LiveAgent 2>&1 | tee -a "$report"
check_status=${pipestatus[1]}
printf '\nExit status: %s\nReport: %s/%s\n' "$check_status" "$PWD" "$report" | tee -a "$report"
printf '\nPress Enter to close.\n'
read -r
exit "$check_status"
