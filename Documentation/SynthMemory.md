# Local synth memory

The Agent shares a local `synth.md` between Standalone and VST3 instances for the
current operating-system user. It reloads the file on every new Agent request.
The file is created on the first successful preference update, not on startup.

- Windows: `%APPDATA%\Super Bass Fully Agentic Dexed\synth.md`
- macOS: `~/Library/Application Support/Super Bass Fully Agentic Dexed/synth.md`

Examples:

- “记住，我平时喜欢温暖柔和的 pad，不喜欢刺耳的高频。”
- “以后 pad 改成偏明亮的，更新我的偏好。”
- “这一次做个明亮的 pad，不要改变我的长期偏好。”
- “忘记我的 pad 偏好。”
- “清空你记住的所有音色偏好。”

The model can call `update_synth_memory` with `remember`, `forget`, or `clear`.
Each action must quote evidence from the current user message. The prompt tells
the model to retain only explicitly expressed enduring sound preferences, not
infer preferences from one-off requests or store unrelated personal information.
This semantic selection is model-driven; exact quotation and file validation
are enforced by the host. A successful write is acknowledged in natural language.

## Privacy and control

Storage is local, but its contents are included in requests to your selected
model provider so the Agent can use your preferences. No extra summarization API
request is made. Chat transcripts, API keys, synth snapshots and audio are not
automatically written to this file. Credential-like content and the current
provider secret are rejected both when writing and when reading model context.
This is defense in depth, not a general-purpose sensitive-data classifier: do not
put secrets in a manually edited file.

Edit the UTF-8 Markdown file directly to inspect or adjust it; delete it to forget
all stored information. Managed entries look like `- [pad_brightness] 温暖柔和`.
Updates to the same key replace earlier entries. Other handwritten prose is
preserved; `clear` removes all notes. Exact quotes are validated but are not saved.

Current requests take precedence over remembered preferences. Memory is passed
as data rather than appended to system instructions. Stored preferences cannot
authorize infinite sustain; the existing current-request requirement remains.

“回退” still restores the sound before the last user request. It does not undo
preference writes: ask to forget or correct preferences separately. Successful
memory writes persist immediately, including if later sound-design steps fail
or the remaining request is cancelled.

## Reliability

All I/O runs on the Agent worker, never in the audio callback. A process mutex and
per-file interprocess lock serialize read-modify-write operations, and a temporary
file replaces the target only after writing completes. Concurrent instances merge
different topic keys; the latest committed value wins for a shared key. Reads are
limited to 8 KiB, individual preferences to 512 UTF-8 bytes. Invalid, oversized,
credential-containing or unreadable files are omitted from model context, and
write failures return a tool error without silently claiming success. Correct or
delete a rejected local file manually.

This feature branch builds on R6. Tests cover persistence, Chinese text, edits,
forgetting, clearing, concurrent writers, credential rejection, bounded reads,
unwritable targets and the model tool loop. Native macOS verification must be run
on a Mac; Windows results are not a substitute for that check.

Verified on Windows, 2026-10-07: Standalone, VST3 and test targets build in Release;
20,607 unit assertions pass, including 41 storage assertions and the session
memory integration scenarios. The opt-in real DeepSeek test
`AgenticDexedTests --filter LiveMemory` passes 13 assertions across remember,
recall and clear requests. It uses an isolated temporary memory file and requires
`DEEPSEEK_API_KEY`; ordinary CTest never runs paid model calls. VST3 pluginval at
strictness 8 passes. The obsolete Intel-Mac workflow assertion inherited from R6
was corrected to match the published Windows x64 / macOS ARM64 workflow.
