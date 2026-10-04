# super-bass-fully-agentic-dexed

基于 Dexed 的原生 FM 合成器：VST3 + 独立应用，支持 Windows x64 和 Apple Silicon macOS，以及自然语言音色设计。

- [Windows 源码与构建说明](../../tree/windows)
- [Apple Silicon macOS 源码与构建说明](../../tree/apple-silicon-macos)
- [发布分支](../../tree/release)

聊天仅显示自然语言，支持中文输入与回车发送。回退会恢复到本次用户输入之前的完整音色；保存为预设保留完整设置。默认要求松键后尾音衰减，除非明确要求无限延音。

## 许可证

GNU GPL version 3，沿用 Dexed 的开源许可，保留上游与第三方声明。详见 [LICENSE](../../blob/windows/LICENSE) 和 [第三方声明](../../blob/windows/THIRD_PARTY_NOTICES.md)。

## 发布状态

当前为私有验收候选。两平台原生回归和成品敏感信息扫描完成后才公开发布。main 分支只包含此 README；构建产物会放入 release 分支。

模型密钥由使用者自行填写，不随源码或应用分发。
