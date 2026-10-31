# Win7 主画布诊断候选最终验证记录

记录日期：2026-10-02（Asia/Shanghai）

## 源码现场

- 分支：chore/publish
- 当前 HEAD：f42083f4f0b5b969b25abc68672df0eeda070f75
- 没有执行 reset、clean、commit、push 或发布。
- 原有未提交 research 文件和四个 inkStrokeModelerTest/*.cso 保留。
- 当前产品改动仍只涉及五个文件：Inkeys/IdtState.cpp、Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp、Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp、Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp、Inkeys/Inkeys/Window/Window.cpp。
- git diff --check：通过。

## 最终候选身份

候选存放在本会话隔离目录：C:\Users\alan-\.codex\visualizations\2026\10\02\01a0faea-01de-77e1-888c-080c6fc5d024\win7-main-canvas-final。

### Debug | ARM64

- 构建：InkeysRepo.sln，Debug|ARM64，/m:1，LinkIncremental=false，完整 solution 退出码 0。
- 构建统计：366 Warning(s), 0 Error(s)；警告来自既有项目类别及关闭增量链接后的 LNK4075。
- EXE SHA-256：D2A28A34327A248F57F9ADD2A76D5B4B0B38DA33F760325E1A44E8649B0BB93A
- Headless SHA-256：FF874C00DB9A55BF15F58018B28632B277585CA8586503D4ADCB0978D6C3C3C9
- PptCOM.dll SHA-256：AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105
- PE：AA64 machine (ARM64)。
- 诊断编译标识：draw3-Oct  2 2026T18:30:07 arch=ARM64 msvc=1944。

### Release | x64

- 构建：InkeysRepo.sln，Release|x64，/m:1，LinkIncremental=false，完整 solution 退出码 0。
- 构建统计：365 Warning(s), 0 Error(s)。
- EXE SHA-256：B3F5A8DFFD53BE274F827D8647BFEDF631EA47017F36DA1C6AD1DD0F20D729D6
- Headless SHA-256：49511AC89FEDECF833B1A3C9A1A5320A6006A43E2063477C680CCCDFE380CD15
- PptCOM.dll SHA-256：AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105
- PE：8664 machine (x64)。
- 诊断编译标识：draw3-Oct  2 2026T18:36:54 arch=x64 msvc=1944。
- 可复制包：C:\Users\alan-\.codex\visualizations\2026\10\02\01a0faea-01de-77e1-888c-080c6fc5d024\win7-main-canvas-final\Inkeys-Win7-Diag-x64-Release.zip
- 包 SHA-256：165126AA9FA7D9B648B3759A036D8D6372EADB15E0A1ED7C653742A03FDD5103

## 验证结果

- ARM64 InkeysHeadlessTests.exe --no-window：退出码 0，PASS animation correctness。
- x64 Release InkeysHeadlessTests.exe --no-window：退出码 0，PASS animation correctness。
- 最终 ARM64/x64 隐藏 Draw3 检查：均退出码 1。日志同时证明两套候选均完成 Drawpad / DrawpadPresentation 创建、首帧 result=committed、ULW event=success、目标 revision 切换与窗口显隐状态记录。
- 隐藏测试中已由旧基线复现的两项失败仍存在：Selection returns to Z before held-contact exit、end-page held contact accepted before real workspace exit。
- 最终候选还报告了持久化、选择内容 revision、resize 等其他隐藏 fixture 失败；这些失败不是本轮改动新增的可证实因果链，也不能用作主画布可见性通过依据。PPT 仍只保留有限回归，未重新展开重构。

## .cso 与现场保护

- 四个用户留存的 inkStrokeModelerTest/*.cso SHA 未变化：
  - inkPixelShader.cso: 3D4701811FF9A5F556C408FA4BC9885444697C793A8059EF63C9A92626E1CE85
  - inkVertexShader.cso: CA9A83348E43B12FF3AD059EC4F1DB47338720C77C0EF897002AEDEC63214B08
  - laserParticleEmitCS.cso: 46DE79F93D6C181E941CCBC0B406D278AFE83F73839017F4A956DEB3DEC21523
  - laserParticleUpdateCS.cso: EF3F2373AF2275A4900594A865B4B93B7E1861AF16506D989DCCA374DD56107D
- 生产 Draw3 资源 .cso 也保持原哈希；构建没有改动交换模式、DComp/ULW、HTTP 回退、旧兼容开关、PPT 数据保护、动画/光影/质量设置。

## 结论与现场下一步

当前机器没有复现 Win7 主画布消失，因此没有做猜测性窗口或呈现策略修复。本轮交付的是范围明确的 Draw3-only 诊断候选；Win7 SP1 + 仅 KB2670838 的用户可见验收仍为“待用户实测”：

1. 启动后主画布出现；
2. 首笔可见；
3. 切换选择模式后桌面输入穿透；
4. 定格切换、返回书写后画布仍可用；
5. 有限确认既有 PPT 流程没有倒退。

请用上面的 x64 Release 候选，在 Win7 上保留完整 IDT log 和 console output，并回传两份原始日志。重点搜索并回传 [Draw3Diag] 行，尤其是 device、window、startup、Draw3Gate、present、ulw，以及窗口是否在首次成功提交后仍为 visible=0。这是继续定因所需的唯一下一步。

