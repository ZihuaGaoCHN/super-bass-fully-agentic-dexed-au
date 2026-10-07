# 自动偏好与预设上下文 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现所有预设共享的自动选择性 `synth.md`，并为每个预设提供可迁移、可压缩、可恢复的连续对话上下文。

**Architecture:** 宿主维护全局偏好和按 UUID 隔离的预设上下文；主 Agent 只消费经过验证的数据，独立后台维护服务负责偏好提取和上下文压缩。`.dexedpreset` 与 DAW 状态携带脱敏压缩上下文，`.syx` 保持原格式并由本机指纹索引关联。

**Tech Stack:** C++17、JUCE 7、JUCE XML/JSON/GZIP、现有 `IModelClient` 与安全凭据存储、CMake/CTest、pluginval、Gitleaks。

**Spec:** `docs/superpowers/specs/2026-10-07-preset-context-auto-memory-design.md`

## Global Constraints

- 当前用户请求优先于摘要和全局偏好；只有当前请求能授权无限延音。
- `synth.md` 全局共享；预设对话按稳定 UUID 隔离。
- `.dexedpreset`/DAW 状态携带上下文；`.syx` 字节完全不变。
- 不持久化推理内容、API Key、Authorization 头、音频或原始大体积工具状态。
- 全局偏好最多 8 KiB；摘要最多 24 KiB；最近至少 8 个完整用户轮次；组装请求最多 224 KiB，并为协议封装预留至少 32 KiB。
- 单预设解压上下文最多 2 MiB；单条消息最多 48 KiB；近期完整轮次最多 32 轮。
- 超过 160 KiB、未压缩轮次超过 12 轮或预计超预算时触发压缩。
- 文件与网络工作不得进入音频回调；写盘使用锁、临时文件和原子替换。
- 不新增主操作按钮或滑动菜单；对话框只显示自然语言。
- GitHub Actions 保持关闭；验证在本机执行。

## Review Focus

- Host 在后台写入期间请求插件状态：应读取完整的不可变缓存快照，不阻塞音频且不产生半写数据；Task 6 覆盖。
- 外部 `.dexedpreset` 包含压缩炸弹、错误哈希或注入指令：音色仍可加载，上下文被隔离；Task 6 覆盖。
- 相同 `.syx` 音色出现在不同文件或槽位：按内容指纹关联同一已有身份，复制操作显式生成新身份；Task 3 覆盖。
- 一轮结束后用户立即更换 provider/model 或关闭插件：维护任务使用该轮捕获的配置，凭据临时加载并安全取消；Task 8 覆盖。
- 用户在普通对话中粘贴未知格式秘密：已知模式必须脱敏，未知模式不能承诺识别，README 要求分享预设前检查；Task 2 和 Task 10 覆盖。

---

### Task 1: 定义上下文领域模型和版本化序列化

**Files:**
- Create: `Source/agent/context/ConversationTypes.h`
- Create: `Source/agent/context/ConversationSerialization.h`
- Create: `Source/agent/context/ConversationSerialization.cpp`
- Create: `Tests/ConversationSerializationTests.cpp`
- Modify: `Source/CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `ConversationMessage`, `ConversationTurn`, `PresetConversationContext`, `PresetContextView`；`serializeContext(const PresetConversationContext&) -> juce::var`；`parseContext(const juce::var&) -> ContextParseResult`。

- [ ] **Step 1: 写失败测试**：覆盖版本 1 往返、中文、工具配对摘要、48 KiB 单消息限制、2 MiB 总上限、非法角色/UTF-8/字段/版本拒绝，以及模型推理字段无法进入结构。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "Conversation serialization"`**，确认因接口不存在而失败。
- [ ] **Step 3: 实现不可变领域结构和严格 JSON 解析**；解析使用累加大小预算，未知安全关键字段拒绝，普通未来字段按版本策略处理。
- [ ] **Step 4: 重跑过滤测试**，期望全部通过。
- [ ] **Step 5: 提交**：`git commit -m "Add versioned preset conversation model"`。

### Task 2: 集中式敏感信息过滤

**Files:**
- Create: `Source/agent/memory/SensitiveDataFilter.h`
- Create: `Source/agent/memory/SensitiveDataFilter.cpp`
- Create: `Tests/SensitiveDataFilterTests.cpp`
- Modify: `Source/CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `SensitiveDataFilter::sanitizeForContext(std::string_view, std::string_view exactCredential) -> SanitizedText`；`SensitiveDataFilter::acceptPreference(...) -> bool`。
- Consumes: Task 1 的消息文本字段。

- [ ] **Step 1: 写失败测试**：精确凭据、UTF-16 表示、`sk-`/GitHub/Google/AWS/Slack token、Bearer、私钥、带密码 URL、本机路径、Tailscale 地址、NUL/控制字符、大小写变体；断言上下文替换为 `[已隐藏敏感信息]`，偏好直接拒绝。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "Sensitive context data"`**，确认失败原因是过滤器不存在。
- [ ] **Step 3: 实现单一过滤器**，不在日志和错误中返回命中原文；保留正常中文与合成器参数名。
- [ ] **Step 4: 重跑测试并加入随机边界输入**，期望无崩溃、无原文泄漏。
- [ ] **Step 5: 提交**：`git commit -m "Filter secrets from persistent Agent context"`。

### Task 3: 稳定预设身份与 `.syx` 指纹索引

**Files:**
- Create: `Source/agent/context/PresetIdentityService.h`
- Create: `Source/agent/context/PresetIdentityService.cpp`
- Create: `Tests/PresetIdentityTests.cpp`
- Modify: `Source/CMakeLists.txt`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces: `PresetId`；`using CanonicalVoice = std::array<std::uint8_t, 155>`；`activateVoice(const CanonicalVoice&) -> PresetActivation`；`updateFingerprint(PresetId, const CanonicalVoice&)`；`clonePreset(PresetId) -> PresetId`；`moveSlot(int,int)`；`renamePreset(PresetId)`；`exportIndex()/importIndex()`。
- Consumes: Task 1 的 `PresetConversationContext::presetId`。

- [ ] **Step 1: 写失败测试**：新音色生成 UUID、重复指纹恢复身份、编辑后旧/新指纹均命中、复制生成新 UUID 并标记克隆来源、移动/重命名保持 UUID、损坏索引不覆盖有效内存状态、相同 `.syx` 内容命中已有身份。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "Preset identity"`**，确认失败。
- [ ] **Step 3: 实现基于规范化 155 字节音色的 SHA-256 索引**；用版本号和原子文件保存 `preset-index.json`。
- [ ] **Step 4: 重跑测试**，并校验 `.syx` 测试文件哈希在操作前后完全一致。
- [ ] **Step 5: 提交**：`git commit -m "Track stable identities for preset conversations"`。

### Task 4: 原子上下文存储和全局偏好存储

**Files:**
- Create: `Source/agent/context/ConversationContextStore.h`
- Create: `Source/agent/context/ConversationContextStore.cpp`
- Modify: `Source/agent/memory/SynthMemory.h`
- Modify: `Source/agent/memory/SynthMemory.cpp`
- Replace/Extend: `Tests/SynthMemoryTests.cpp`
- Create: `Tests/ConversationContextStoreTests.cpp`

**Interfaces:**
- Produces: `load(PresetId) -> ContextLoadResult`；`commit(PresetConversationContext, expectedVersion) -> ContextCommitResult`；`appendTurn(PresetId, ConversationTurn, expectedVersion)`；`loadPreferences()`；`applyPreferenceDiff(ValidatedPreferenceDiff)`。
- Consumes: Task 1 序列化、Task 2 过滤、Task 3 `PresetId`。

- [ ] **Step 1: 写失败测试**：中文持久化、多进程锁、并发版本冲突重读合并、临时文件失败保留旧数据、软链接拒绝、8 KiB/2 MiB 上限、无凭据或私人路径写入、手工 `synth.md` 内容保留。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "context store"` 和 `--filter "Local synth memory"`**，确认新断言失败。
- [ ] **Step 3: 实现 `contexts/<uuid>.json.z`、`memory-state.json` 和原子写入**；GZIP 解压必须有输出上限。
- [ ] **Step 4: 重跑两组测试**，期望通过且目录无残留临时文件。
- [ ] **Step 5: 提交**：`git commit -m "Persist global preferences and per-preset context"`。

### Task 5: 上下文组装、连续对话和预算管理

**Files:**
- Create: `Source/agent/context/ContextManager.h`
- Create: `Source/agent/context/ContextManager.cpp`
- Modify: `Source/agent/session/AgentSession.h`
- Modify: `Source/agent/session/AgentSession.cpp`
- Modify: `Source/agent/AgentController.h`
- Modify: `Source/agent/AgentController.cpp`
- Modify: `Tests/AgentSessionTests.cpp`
- Create: `Tests/ContextManagerTests.cpp`

**Interfaces:**
- Produces: `ContextManager::buildRequestContext(PresetId, currentRequest) -> ContextBuildResult`；`AgentSession::loadConversation(PresetContextView)`；终态回调 `ITurnSink::onTurnFinished(TerminalTurn)`。
- Consumes: Task 4 存储和现有 `ModelMessage`。

- [ ] **Step 1: 写失败测试**：第二轮包含第一轮、不同 UUID 不串线、全局偏好共享、当前请求位于最后、工具调用/结果成对、224 KiB 上限且保留 32 KiB 协议空间和最近 8 轮、无限延音只能由当前请求授权、切换预设恢复 UI 自然语言历史。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "Managed Agent context"`**，确认当前 `messages_.clear()` 行为使连续对话测试失败。
- [ ] **Step 3: 实现 ContextManager 并让 AgentSession 每轮重建受预算约束的消息序列**；删除主 Agent 的 `update_synth_memory` schema 和显式写记忆路径。
- [ ] **Step 4: 重跑测试和现有 `--filter "Agent session"`**，期望旧回退/工具协议行为不变。
- [ ] **Step 5: 提交**：`git commit -m "Add continuous per-preset Agent context"`。

### Task 6: `.dexedpreset`、DAW 状态与上下文迁移

**Files:**
- Create: `Source/agent/context/PortablePresetContext.h`
- Create: `Source/agent/context/PortablePresetContext.cpp`
- Modify: `Source/PluginData.cpp`
- Modify: `Source/PluginProcessor.h`
- Modify: `Source/PluginProcessor.cpp`
- Modify: `Source/ui/MainEditor.cpp`
- Modify: `Tests/StateCompatibilityTests.cpp`
- Create: `Tests/PortablePresetContextTests.cpp`

**Interfaces:**
- Produces: `encodePortableContext(PresetConversationContext) -> MemoryBlock`；`decodePortableContext(MemoryBlock) -> PortableContextResult`；`AgentController::portableContextSnapshot()`；`AgentController::importPortableContext(...)`。
- Consumes: Task 4 缓存快照；禁止在 `getStateInformation` 中等待磁盘或网络。

- [ ] **Step 1: 写失败测试**：新版往返、跨实例/跨机器导入恢复 UUID 和上下文、旧状态兼容、缺失节点、错误 SHA-256、2 MiB 解压上限、压缩炸弹、注入文本仅作为数据、损坏上下文时音色仍成功加载、Host 保存与后台 commit 并发时导出完整快照。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "Portable preset context"` 和 `--filter "State compatibility"`**，确认失败。
- [ ] **Step 3: 在 `dexedState` 增加版本化 `agentContext` 子节点**，使用 GZIP JSON、哈希和不可变内存缓存；载入时先验证再异步导入。
- [ ] **Step 4: 保存 `.dexedpreset` 后检查其中不含凭据、绝对路径或未脱敏对话；重跑测试。**
- [ ] **Step 5: 提交**：`git commit -m "Carry safe Agent context with native presets"`。

### Task 7: 预设生命周期和界面上下文切换

**Files:**
- Modify: `Source/ui/presets/PresetLibraryService.h`
- Modify: `Source/ui/presets/PresetLibraryService.cpp`
- Modify: `Source/ui/MainEditor.cpp`
- Modify: `Source/ui/AgentPanel.cpp`
- Modify: `Source/agent/AgentController.h`
- Modify: `Source/agent/AgentController.cpp`
- Modify: `Tests/PresetLibraryServiceTests.cpp`
- Modify: `Tests/AgentPanelTests.cpp`

**Interfaces:**
- Produces: `AgentController::activatePreset(PresetActivation)`；`clonePresetContext(source,destination)`；`movePresetContext(source,destination)`；`currentConversation()`。
- Consumes: Task 3 身份、Task 5 会话载入、Task 6 portable import。

- [ ] **Step 1: 写失败测试**：切换槽位切换聊天记录、重命名不变、移动跟随、复制克隆后独立、初始化清空、新 `.dexedpreset` 另存沿用上下文、浏览 `.syx` 通过指纹恢复；界面不显示工具 JSON且按钮仍只有发送/回退/保存。
- [ ] **Step 2: 运行 `AgenticDexedTests --filter "Preset library"` 和 `--filter "Agent panel"`**，确认失败。
- [ ] **Step 3: 在所有预设入口调用统一生命周期接口**；预设切换取消进行中的主请求，持久化任务继续写原 UUID。
- [ ] **Step 4: 重跑测试，并进行 Standalone 中两个预设来回切换的手工冒烟测试。**
- [ ] **Step 5: 提交**：`git commit -m "Bind Agent conversations to preset lifecycle"`。

### Task 8: 后台自动偏好整理与上下文压缩

**Files:**
- Create: `Source/agent/memory/PreferenceCurator.h`
- Create: `Source/agent/memory/PreferenceCurator.cpp`
- Create: `Source/agent/context/ContextCompactor.h`
- Create: `Source/agent/context/ContextCompactor.cpp`
- Create: `Source/agent/memory/MemoryMaintenanceService.h`
- Create: `Source/agent/memory/MemoryMaintenanceService.cpp`
- Modify: `Source/agent/AgentController.cpp`
- Create: `Tests/PreferenceCuratorTests.cpp`
- Create: `Tests/ContextCompactorTests.cpp`
- Create: `Tests/MemoryMaintenanceTests.cpp`

**Interfaces:**
- Produces: `PreferenceCurator::buildRequest/parseResponse`，只接受 `add/replace/remove/observe/none`；`ContextCompactor::buildRequest/parseResponse`；`MemoryMaintenanceService::enqueue(TerminalTurn, ProviderConfig, credentialId)`；`shutdown()`。
- Consumes: Task 2 过滤、Task 4 存储、Task 5 `TerminalTurn`，现有 `IModelClient`/`ICredentialStore`。

- [ ] **Step 1: 写失败测试**：无需“记住”的明确长期偏好自动写入；一次性要求只 observe；两个不同轮次重复才晋升；否定和冲突替换；`add/replace/remove/observe/none` 之外操作拒绝；证据必须逐字存在于当前用户文本；无关个人信息拒绝；整理器无合成器工具；压缩保留最近 8 轮和结构字段；失败/离线/无效 JSON 不删原文；取消轮延后；provider 切换使用轮次捕获配置；关闭时取消且凭据清零。
- [ ] **Step 2: 运行三个新过滤测试**，确认接口不存在而失败。
- [ ] **Step 3: 实现串行后台维护队列和严格 JSON 协议**；主回答先完成，维护状态只发简短诊断事件。
- [ ] **Step 4: 重跑新测试并验证后台请求 `tools.size()==0`、参数写入次数为 0。**
- [ ] **Step 5: 提交**：`git commit -m "Automatically curate and compact Agent memory"`。

### Task 9: 真实模型连续上下文回归

**Files:**
- Modify: `Tests/AgentSessionTests.cpp`
- Modify: `Tests/TestMain.cpp`
- Modify: `packaging/macos/Run-Full-Regression.command`

**Interfaces:**
- Consumes: Tasks 5–8 的完整系统。

- [ ] **Step 1: 新增 opt-in `LiveMemory` 测试**：真实 DeepSeek 执行“一次性明亮 pad（不记）→ 明确长期温暖偏好（自动记）→ 新预设询问偏好（继承全局、不继承旧对话）→ 长上下文压缩后追问 → 清除测试数据”。
- [ ] **Step 2: 先运行测试**，确认自动维护尚未满足时失败。
- [ ] **Step 3: 只调整维护提示和协议边界以满足测试**，不为固定文案硬编码结果。
- [ ] **Step 4: 运行 `AgenticDexedTests --filter LiveMemory`**，期望全部通过、音色写入只发生在明确要求调音的轮次、临时测试目录最终删除。
- [ ] **Step 5: 提交**：`git commit -m "Cover automatic memory with real model regression"`。

### Task 10: 中文 README、更新日志和用户文档

**Files:**
- Modify: `README.md`
- Modify: `Documentation/SynthMemory.md`
- Modify: `Documentation/BuildingAgenticDexed.md`
- Create: `CHANGELOG.zh-CN.md`

**Interfaces:**
- Consumes: 最终实现中的确切路径、限制和行为。

- [ ] **Step 1: 写文档验收检查**：README 必须包含自动选择性记忆、所有预设共享偏好、每预设独立上下文、文件位置、模型数据流、清除方法和隐私提示；更新日志包含 Added/Changed/Security/Validation 中文章节。
- [ ] **Step 2: 运行文档检查并确认因缺少章节失败。**
- [ ] **Step 3: 更新中文文档**；说明 `.dexedpreset` 会携带脱敏上下文、`.syx` 不变、分享前仍需检查，且不声称能识别全部个人信息。
- [ ] **Step 4: 重跑文档检查，并人工核对没有将测试结果写成尚未完成的事实。**
- [ ] **Step 5: 提交**：`git commit -m "Document automatic memory and preset context in Chinese"`。

### Task 11: 全量验证、敏感信息审计和 feature 分支交付

**Files:**
- Modify: `Documentation/ValidationMatrix.md`
- Create: `Documentation/releases/2026-10-07-auto-memory-feature.md`
- Output only: `build/auto-memory-audit/*`

**Interfaces:**
- Consumes: 所有任务产物；不产生新的运行时接口。

- [ ] **Step 1: Windows Release 构建**：`cmake --build build/synth-memory --config Release --target AgenticDexedTests AgenticDexed_Standalone AgenticDexed_VST3 --parallel 2`；期望退出 0。
- [ ] **Step 2: 完整 CTest**：`ctest --test-dir build/synth-memory -C Release --output-on-failure`；期望 100% 通过。
- [ ] **Step 3: pluginval strictness 8**：运行 `scripts/validate-plugin.ps1`；期望 `SUCCESS`。
- [ ] **Step 4: 真实模型测试**：运行 `AgenticDexedTests --filter LiveMemory`；记录模型、时间、断言数和临时数据清理结果，不记录 Key。
- [ ] **Step 5: Windows 手工冒烟**：两个预设连续对话、切换、保存 `.dexedpreset`、重启、恢复、清空偏好；记录结果。
- [ ] **Step 6: Apple Silicon 本机验证**：ARM64 Standalone/VST3、完整测试、pluginval、真实模型、保存重启和 preset 往返；在拿到本机报告前明确标记未完成。
- [ ] **Step 7: 敏感信息扫描**：对 feature 全历史、工作树、测试日志、Standalone/VST3、`.dexedpreset`、ZIP/TAR 运行 Gitleaks 与递归扫描；包含已知环境 Key 的 UTF-8/UTF-16 精确匹配和私人路径检查；期望无未解释命中。
- [ ] **Step 8: 更新验证矩阵和中文 feature 更新记录**，只写有证据的通过项和剩余限制。
- [ ] **Step 9: `git diff --check`、确认工作树干净，提交验证记录并推送 `feature/local-synth-memory`；GitHub Actions 仍为 disabled。**
