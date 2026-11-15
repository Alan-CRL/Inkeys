# 执行

- 依赖前四子任务。ARM64原生MSBuild完整solution及共享测试、独立检查与spec同步。
- CPU/UInk/生产GPU/Headless通过，编码diff检查无无关变更；真机待验项明确。
- 验证命令与结果归档在父任务validation.md；失败先定位首个错误。
- 不启动GUI，不使用自动提交/归档脚本。
