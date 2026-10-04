Agentic Dexed — ARM native agent reliability candidate, 2026-10-03 r2

本包包含 ARM 原生独立应用、VST3，以及 ARM 原生 Agent 检查程序。
不包含源码，不需要远程登录，也不需要 Rosetta。

1. 退出旧版 Agentic Dexed 和正在使用它的 DAW，再打开本包的 Agentic Dexed.app。
2. 在 Agent 设置中选择 Chat Completions，地址 https://api.deepseek.com，模型 deepseek-flash。
   使用自己的 API Key。密钥不包含在本包中。
3. 双击 Run-Agent-Check.command 执行真实生成回归。
   检查程序读取已保存到 macOS 钥匙串的密钥；如钥匙串询问访问权限，允许该检查程序访问。
   它会调用真实 API，修改临时处理器中的音色并离线渲染，检查无静音、无削波以及松键两秒后的尾音。
   然后检查连续修改、确认前后状态，以及取消后参数与版本号保持不变。确认步骤由测试程序自动执行。
   它不会修改打开的应用、DAW 工程或已保存音色。
4. 检查结束后，同目录生成 Agent-check-日期时间.txt。测试失败时请把报告发回。
5. 在独立应用中输入“空灵的pad，余音很长”，弹奏试听；VST3 放入用户插件目录后在 DAW 中重扫并测试。

已验证：Windows 完整自动化回归、Windows VST3 pluginval 严格度 8、多个真实 DeepSeek 音色生成和音频检测。
ARM 交叉编译和二进制架构检查不等于 Mac 运行验证。本包不宣称“所有 bug 已修复”。
详细检测结果见 agent-reliability-2026-10-03.md。
