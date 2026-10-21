# 原 Freeze Mag PrintWindow 检查（2026-10-21）

按当前 prd.md、design.md、implement.md 与 native-desktop/scroll-follow-capture.md 审查。本轮结论：**无 callback 的原 Freeze Mag 软件内读回准备验证通过，可以按用户要求结束本轮。** 未发现需要阻塞本轮或修复生产代码的问题；不继续 Tracker、Viewport、Bar、Draw3 或 UInk 阶段。

## Findings (fixed)

无。检查阶段只新增本报告，未修改生产代码、测试、工程、主会话维护的 spec/任务文件或 implementer 报告。

## Findings (not fixed)

无本轮范围内的已确认代码缺陷。以下是已经明确保留的验收边界，不作为本轮阻塞：当前仅 Win11 ARM64 受控图案；未验持续服务、20FPS、其他平台、用户文档、笔/触摸、锁屏、显示拓扑变化和系统调用永久阻塞。GDI 11→13、USER 4→5 已披露，正常进程清理不能作为长期资源有界性证明。

## 核对结果

- 已移除独立 MagCaptureService、MagnificationRuntime、旧 capture 测试及登记；排除 Cache/第三方后对生产与 Headless 源码搜索无旧引用。Window.cpp、MagnifierCoordinator、两个 vcxproj、filters 和 Headless runner 与 1b8cb4b6 基线无 diff。
- 原 WindowService 的 Overlay STA、CreateGroup、beforeCreate/created/destroyed、readyPromise 调用链保留；只调用原 PrepareMagnifierWindow/HostCreated/ChildCreated，初始化与创建同 owner。普通 MagnifierThread 未重排。新增读回定时消息复用该原 HWND，普通 Freeze 前后仍由既有 coordinator 执行。
- 无 callback 从冷 alpha0 窗口对开始：phase1–3 先于首次 opaque Reveal 和 callback 登记；phase1 静止读三次。phase6 后恢复原 nullptr，再读取新 phase7。旧帧/不完整帧不被当作通过，有限重试不刷新当前步骤的5秒截止。run2 首张62/63被拒绝后重试成功，如实保留。
- run2/PID73216、run3/PID81464 各9个接受样本63/63，其中各6个无 callback；初期 callback 计数0，登记后各6入6出，恢复后计数不变。callback 只作同格式有界直通和原子计数，实际输出来自 PrintWindow(child, dc, 2)。日志与源码均不把 callback 退出当成通用整帧完成保证。
- 蓝覆盖窗有屏幕 GetPixel 正对照，并实际包含于原角色过滤表；SOURCE 不在过滤表。63点包括覆盖区域。14张保存 PNG 的离线记录各5,526,720像素全匹配；它们的尺寸2880×1919，与真实 child/DIB 一致，原 Mag source 为2879×1919。未把静止重复的未保存帧或被拒绝帧计入整图核对。
- 入口在配置、单实例和普通业务前严格早退，额外参数及关闭 harness 返回2。未调用配置写入、WGC 或捕获禁止属性。正常/步骤失败经 Finish 恢复 callback、隐藏和释放 DIB；主清理先停 coordinator/join，再 StopAndJoin WindowService，最后撤销 SOURCE/句柄/logger。45秒最终进程截止不被算作正常清理。本轮两次日志均为正常销毁、同 owner MagUninitialize 成功、最终 result=0。

## Verification

- Lint：通过本轮重新执行的 git diff --check、三份工程 XML 解析、七个修改源/工程文件严格 UTF-8 BOM+CRLF 检查。未发现独立 lint/clang-tidy 目标；已有 i18n 日志两语言均425/425通过。
- TypeCheck：通过，复用当前最终源码对应的完整 InkeysRepo.sln Debug|ARM64 构建，19.09秒、3个既有 hashlib++ C4267、0 error。日志 freeze-printwindow-final-build-arm64.log / -console.log；主会话记录该命令 exit0。源文件早于构建，当前 EXE SHA-256 仍为 E616D3D903D9F9535AE6C6C786B9EE6D8E13C304171062DDBEC2C2DBB1407F07。检查未改源码，故不重复构建。
- Tests：通过，复用并解析上述 run2/run3 原始日志与 freeze-printwindow-offline-pixels.json，断言每轮9接受/6无callback及最终销毁成功；复核现有 --no-window 日志末尾 PASS animation correctness，主会话记录 exit0。非法参数既有验证 exit2。本检查未重新启动窗口、使用 Computer Use 或执行普通 GUI。

Build/ScrollFollow 下的日志、PNG 和离线 JSON 是生成证据，不纳入提交；没有 commit/push。
