# Super Bass Fully Agentic Dexed

基于 Dexed 的原生 FM 合成器：VST3 + 独立应用，支持 Windows x64 和 Apple Silicon macOS，以及自然语言音色设计。

- [Windows 源码与构建说明](../../tree/windows)
- [Apple Silicon macOS 源码与构建说明](../../tree/apple-silicon-macos)
- [发布分支](../../tree/release)
- [R6 构建下载与完整源码包](https://github.com/postenginnering/super-bass-fully-agentic-dexed/releases/tag/v1.0.1-r6)

聊天仅显示自然语言，支持中文输入与回车发送。回退会恢复到本次用户输入之前的完整音色；保存为预设保留完整设置。默认要求松键后尾音衰减，除非明确要求无限延音。

## 许可证

GNU GPL version 3，沿用 Dexed 的开源许可，保留上游与第三方声明。详见 [LICENSE](../../blob/windows/LICENSE) 和 [第三方声明](../../blob/windows/THIRD_PARTY_NOTICES.md)。

## 发布状态

本项目以 GPL v3 公开源码，当前提供 R6 预发布候选。Windows 回归与 VST3 检查通过；Mac 原生运行时 19,589 项检查和 VST3 检查通过，但真实模型测试因测试进程未取得密钥而尚未运行。详细验证范围见 release 分支说明。

main 分支只包含此 README；Windows 与 Apple Silicon 源码分别位于对应分支；两平台构建产物已放入 release 分支，并提供 GitHub 预发布下载。完整源码包包含构建所需的 14 个固定版本依赖。GitHub Actions 保持关闭。

模型密钥由使用者自行填写，不随源码或应用分发。
