# 发布基线运行时与构建整合

## 人工冲突解决
- `Inkeys.vcxproj` 使用发布基线的生产静态 CRT（Debug 亦为 MultiThreaded），保留 Fluent 所需 imgui include 路径；ARM64 保留发布 vcpkg include。头文件登记合并 Fluent Design/Typography/Controls/Pages/Layout/Theme 和 Draw3/PPT/Update 所需项。
- filters 合并两侧项目项并修正共享尾标签，XML 解析通过；无重复源码登记。原本重复 PptCOM ProjectReference 为两侧共同历史项，本轮未做无关清理。
- Window.cppm 保留 Fluent 可缩放窗口 frame API，同时加入发布双画布、白板 activation 和 owner 接口；Window.cpp 自动合并保留 QuerySettingClientFrameInsets 与发布 owner/focus、shutdown gate 逻辑。
- Headless 工程保留 ARM64 IMGUI_DISABLE_SSE，并加入发布 INKEYS_MESSAGE_BOX_TESTING/DRAW3_CONTACT_TESTING；保留两侧源码与头文件。
- session tests 合并常驻资源/DPI/frame/主题断言和发布 StartupPreview/主栏按钮焦点三态断言；window tests 保留 Fluent frame fixture 和发布 owner/Z-order/exit visibility 断言。本次只编译 Window tests，不执行任何 HWND 测试。
- 合并冲突的 PptCOM.dll 从固定 e2c212d4 Git 对象读取，避免复制原工作区用户修改；完整 Solution 将重新生成 paired DLL/TLB。

## 配置与国际化审查
- `IdtConfiguration.cpp/.h` 对发布基线仅增加 SettingDarkMode bool 默认值、严格 bool 读取和快照 bool 写入。
- `Other.Config.cpp/.cppm` 规范化换行后与 e2c212d4 完全一致，保留发布默认/范围/实时消费者。
- 发布配置退出 AvoidFullScreen/PointAdsorption/SmoothWriting/HideTouchPointerBeta/Performance/Preset，CaptureSettingJson 在 AutoClean=false 时也显式清理；Save.Enable/SaveDays 保持 Draw3 活动配置。PPT FixedHandWriting 不读写。
- 三语言自动合并保留发布内容，并追加 Fluent Design 键与松开滑块即应用 SettingUIScale 说明。i18n check 简繁英当前 366/366 100% 通过；逐叶对照确认发布键全部存在，34项额外键均为 Fluent Design（最终页面改动后需再次验证）。
- 已修改工程/Window/测试及自动合并配置的 BOM 和 CRLF 均保持；XML 正常，无重复源码项；局部 worktree 和 staged diff --check 通过。

## 独立构建环境
- vswhere 定位 ARM64 原生 MSBuild，安装路径由当前 VS 动态发现。
- Vcpkg SHA 99a97de2cb371449d4fb9dc970f2ac562d689ec2，HiMsg SHA e7f42dc108bbd4201486e769648ef83240a90be2，均与发布基线一致。
- 新 worktree 缺少子模块工作文件、Package 和 VcpkgInstalled；从原工作区复制固定版本源码及已安装 ARM64 库到独立 worktree，robocopy 均退出1（正常新增）。最终使用独立 Git 元数据/index/refs（Inkeys/Cache/DependencyGit），两子模块 show-toplevel/gitdir 均在隔离工作区；本地 tools 缓存亦独立复制。原依赖 cached/working diff 均为空、SHA 与开始前一致。
- 原工程 None 登记的 src/ui1.bin 缺失属于历史项，不改项目规避；实际构建是否引用由结果判断。

## 验证范围
仅执行完整 Solution 与 --no-window 回归；最终退出码见下文。GUI、Snap、真实跨屏/鼠标及主题外观本轮未授权，保留实机验收。

## 首次构建及环境定位（2026-10-08）
- ARM64 native MSBuild 完整 Solution /m:1 /nr:false，900秒允许，实际54.5秒退出1；PptCOM/PptCOM.Tests/Headless 已编译，主Inkeys在 vcpkg manifest install 前置失败，尚未编译主源码。
- 首个有效错误：vcpkg internal error files.cpp(338) Value was null / MSB3073 exit1。复现发现 Remove-Item Env:PATH 后 Test-Path Env:Path=false；vcpkg 需要 PATH 才能查找 Git/工具。下一轮同一 invocation 保存原Path→移除PATH→恢复单一Path→设置 MSBUILDDISABLENODEREUSE，不改项目/依赖版本。
- 官方固定版本 vcpkg-tool 源码的 calculate_path_bases 直接要求 PATH（https://github.com/microsoft/vcpkg-tool/blob/e0612b42ce44e55a0e630f2ee9d3c533a63d8bc1/src/vcpkg/base/files.cpp）。诊断安装在保留Path时能前进，随后因缺本地Ninja缓存而失败；缓存tools已从本机复制到独立工作区。
- vcpkg debug 显示 read-tree 提取版本，因此预防性方案尝试恢复主依赖索引被自动审批拒绝；含 write-tree 的核验亦被拒绝，均未执行。实际只读 diff 证明原Vcpkg/HiMsg cached/working均clean，无需恢复。后续改用普通文件复制完整独立git metadata，所有config只改副本；不再写主索引。
- 第一轮成功生成的 Headless 运行 --no-window，EXIT=0 / PASS animation correctness；主程序完整构建仍待后续验证，未声称通过。测试日志 headless-no-window.log。

## 第二轮完整 Solution
- 同一 PowerShell invocation 保存原Path、Remove-Item Env:PATH、恢复单一Path并设置 MSBUILDDISABLENODEREUSE=1；进程环境 Path键数断言为1。
- vswhere 动态定位 ARM64 native MSBuild，InkeysRepo.sln Debug|ARM64 /m:1 /nr:false，900秒允许，实际486.7秒，BUILD_EXIT=0。
- 独立vcpkg目录按照固定manifest重建所需包，未改变包版本、SDK、项目配置；后续完成Inkeys、PptCOM、PptCOM.Tests、Headless编译/链接。
- 新增直接使用生产 ResolveLinkedPptScaleDraft 的连续中组/底组拖动、空闲帧、默认值Reset、unlinked/join案例；避免未保存Record回滚本帧编辑。
- DrawDescription 三语言改为笔迹与橡皮/筆跡與橡皮/Ink and eraser options，并同步基准snapshot；最终i18n sync+check 366/366 100% PASS，header无额外diff，新增改动保持UTF-8/CRLF。
- 警告包含既有HiMsg匿名union、hashlib类型转换、设置/Bar浮点转换、测试局部变量遮蔽等；未为零warning做无关修改。
- 最后四处General section标题键修正已纳入下列最终增量完整Solution及重新编译后的--no-window；首版测试仅保留诊断历史。


## 最终前置验收（2026-10-08）
- General 四个分组标题采用发布原键 Regular.StartUp.N / Appearance.N / Behavior.N / Tentative.N；Setting.Pages.cpp 在最后完整 Solution 增量中重新编译，Inkeys 链接成功。
- 最后完整 InkeysRepo.sln Debug|ARM64 /m:1 /nr:false，ARM64 native MSBuild 由 vswhere 动态定位；同一 invocation 保存Path→Remove-Item Env:PATH→恢复单一Path→MSBUILDDISABLENODEREUSE=1。900秒允许，实际13.0秒，BUILD_EXIT=0；日志 solution-build-final-stdout.log / solution-build-final-stderr.log。
- 最后 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，HEADLESS_EXIT=0，末行 PASS animation correctness；完整日志 headless-no-window-final.log。本次产物包含直接调用生产 LinkedPptScaleDraft helper 的全部连续帧场景。未运行 Window HWND 测试、GUI或Office COM实例。
- 最后 Scripts/i18n.ps1 check，EXIT=0；en-US/zh-TW 均366/366，100%。自己负责的15个准确路径对 HEAD 的 git diff --check EXIT=0；全部无BOM且无单独LF，保留原UTF-8/CRLF。Checker额外确认设置界面/配置/生成键路径无新增空白问题。全量合并继承发布历史的空白诊断由主协调另存 inherited-whitespace.json，本轮不改历史文件。
- 原Vcpkg及 Inkeys/additional/HiMsg 的工作树和cached diff均EXIT=0；原版本SHA未变。隔离子模块 show-toplevel 与 absolute-git-dir 均在本工作区，使用独立源码/index/refs/objects（不是原工作区共享可写索引）。此前预防性恢复原索引的命令被自动审批拒绝，实际clean证据确认不需恢复，未执行任何原索引写操作。
- 主工作区 IdtMain.h / PptCOM.dll / PptCOM.Tests 的保护校验由主协调按 original-workspace-preservation.json 再做最终核对；本实现者没有编辑其源码或用户二进制。

## FXC配套生成物核对
- 发布固定e2c212d4没有跟踪Draw3的.cso；Fluent HEAD跟踪4个.cso却没有对应的3个新源 inkPixelShader.hlsl / ink.hlsli / EraserGripVisual.h。合入后的这3个源规范化CRLF后与固定e2逐字节相同，无手工shader源码改动。
- 现有项目的 FxCompile 使用中间目录无BOM源，/E main /T ps_5_0，将输出写回inkPixelShader.cso；没有改FXC参数或Debug选项。
- inkPixelShader.cso 从Fluent已有25744字节（SHA256 2996066ea00d7736b000233d0d968d1ce4741ec14b15ee1507709745b9bc153f）生成28748字节（SHA256 f55ddf08624442c5a3b512897e9d00130e692aa99709a996879ece837f1a69f8）。旧/新均HLSL Shader Compiler 10.1，DXBC没有SDBG/SPDB路径或Debug块；RDEF/OSGN相同，ISGN/SHEX/STAT变化，属于发布新增源的配套指令生成物而非本轮手工UI修改。
- 另外3个Draw3.cso与开始时hash完全一致；PptCOM.dll按完整Solution生成，PptCOM.tlb hash保持。主协调确认保留配套产物。具体前后hash见 pre-build-generated-hashes.json / post-build-generated-hashes.json。
