## 问题与结果

RTS扩展属性设置成功而Enable失败时，Draw3此前直接终止输入启动，缺少独立鼠标兜底；合成矩形还会DISCARD大墨迹缓冲区。此改动补齐基础X/Y和Win32鼠标回退，并为兆芯绘图设备提供结构化缓冲兼容上传路径。

## 变更

- 完整RTS失败后安全清理并使用新实例尝试一次基础X/Y；RTS不可用时由窗口owner启用单路Win32鼠标输入。保留callback排空、候选Down隔离、捕获取消和最终保存/代次隔离。
- 增加默认开启的绘图硬件加速、兆芯兼容偏好及三语言设置。关闭硬件加速时直接WARP；兼容路径仅对实际Hardware adapter VendorId=0x1D17生效，恢复设备沿用本代启动快照。
- 合成矩形改用独立256B动态资源，统一结构化上传及head/offset维护；兼容路径使用DEFAULT局部更新，其他设备保留动态Map能力判断。
- 基于dev仅提取产品修复；不包含诊断分支新增的性能采集会话、资源探针、诊断启动器、输出设置或诊断包脚本。

## 验证

- InkeysRepo.sln：Debug | ARM64，完整solution构建退出0、0错误。
- InkeysHeadlessTests --no-window：完整套件退出0，RTS回退和鼠标生命周期测试通过。
- --draw3-device-policy-test、--config-boundary-test：退出0；新增入口额外参数均退出2。
- --draw3-renderer-upload-test：真实WARP离屏dynamic discard、dynamic append和DEFAULT三模式上传/像素读回通过，无HWND或swapchain。
- 独立代码审查、i18n检查、工程XML及BOM/CRLF/diff检查通过。

真实兆芯硬件、故障RTS设备、窗口捕获/笔触摸提升和设置页GUI仍待现场验证；当前未运行GUI。ARM64结果不替代其他架构/Win7验证。
