#!/bin/zsh
cd -- "${0:A:h}" || exit 1
results="Mac-regression-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$results" || exit 1
report="$results/summary.txt"
finish() {
    local result_code="$1"
    printf '\nExit status: %s\n' "$result_code" >> "$report"
    tar --exclude "$results/pluginval-tool" -czf "$results.tar.gz" "$results"
    printf '\n报告已保存：%s/%s.tar.gz\n' "$PWD" "$results"
    if [[ "$result_code" == 0 ]]; then
        printf '自动检查已通过。还请打开应用检查中文输入，并在 DAW 中试听 VST3。\n'
    else
        printf '检查尚未全部通过。请将报告发回，不必自行排查。\n'
    fi
    printf '按回车关闭。\n'
    read -r
    exit "$result_code"
}
printf 'R4 native Mac runtime regression\n' > "$report"
if [[ "$(uname -m)" != arm64 ]]; then
    printf '请使用原生 Apple Silicon 终端运行，不能使用 Rosetta。\n' | tee -a "$report"
    finish 2
fi
sw_vers >> "$report"
shasum -a 256 ./AgenticDexedTests './Agentic Dexed.app/Contents/MacOS/Agentic Dexed' './Agentic Dexed.vst3/Contents/MacOS/Agentic Dexed' >> "$report"
printf 'Source-only audit and build-script checks run on the build checkout; this package contains runtime data, not source.\n' >> "$report"

printf '1/3 正在运行本机回归测试…\n'
./AgenticDexedTests --portable > "$results/runtime.log" 2>&1
runtime_result=$?
grep 'SUMMARY:' "$results/runtime.log" >> "$report"
if [[ -d build/macos/workbench-render ]]; then
    cp -R build/macos/workbench-render "$results/界面检查"
fi
[[ "$runtime_result" == 0 ]] || finish "$runtime_result"

printf '2/3 正在获取官方插件检查工具并校验下载…\n'
mkdir -p "$results/pluginval-tool"
tool_zip="$results/pluginval-tool/pluginval_macOS.zip"
curl --fail --location --retry 2 --connect-timeout 20 --max-time 180 \
    'https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip' \
    --output "$tool_zip" > "$results/download.log" 2>&1 || finish 2
expected='3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f'
actual="$(shasum -a 256 "$tool_zip" | awk '{print $1}')"
if [[ "$actual" != "$expected" ]]; then
    printf 'Official pluginval archive checksum mismatch\n' >> "$report"
    finish 2
fi
unzip -oq "$tool_zip" -d "$results/pluginval-tool" || finish 2
validator="$results/pluginval-tool/pluginval.app/Contents/MacOS/pluginval"
chmod +x "$validator"
arch -arm64 "$validator" --strictness-level 8 --timeout-ms 120000 \
    --sample-rates '44100,48000,96000' --block-sizes '1,32,64,512,1024' \
    --output-dir "$PWD/$results/pluginval" \
    --validate "$PWD/Agentic Dexed.vst3" > "$results/pluginval-console.log" 2>&1
plugin_result=$?
printf 'VST3 pluginval exit: %s\n' "$plugin_result" >> "$report"
[[ "$plugin_result" == 0 ]] || finish "$plugin_result"
if ! grep -Rq 'SUCCESS' "$results/pluginval"; then
    printf 'Pluginval did not produce successful validation evidence\n' >> "$report"
    finish 2
fi

printf '3/3 正在使用已保存的密钥检查真实模型调用和整轮回退…\n'
./AgenticDexedTests --portable --filter LiveAgent > "$results/live-agent.log" 2>&1
live_result=$?
grep 'SUMMARY:' "$results/live-agent.log" >> "$report"
finish "$live_result"
