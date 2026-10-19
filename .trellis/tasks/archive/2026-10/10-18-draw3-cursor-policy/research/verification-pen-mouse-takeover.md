# Pen离开后Mouse画笔光标回弹验证（2026-10-19）

用户确认Windows 11复现，鼠标未移动，屏幕笔离开约1秒后出现Mouse画笔光标；默认System样式原本正常。本次在已提交基线e9e9b1f5上修复，未创建新commit或push。

## 修复与证据

Controller实际build路径的primaryRuntime只选择工具，Pen owner配空样本不回退Mouse，原本没有一秒自动恢复规则。已找到的可达缺口是：RTS光标入口在Pen持有期间接受成功查询为IMDT_UNAVAILABLE+IMO_SYSTEM的WM_MOUSEMOVE，随后改owner并发布Mouse样本；System不自绘Mouse主视觉，Drawing会绘制PenIcon，因此同一错误接管可能只在Drawing下可见。

本次仅在现有Filter输入末尾追加默认Unknown的pointerAuthority，由Window现有cursorOwner_的acquire快照接线，将已有Touch系统Move保护扩至Pen owner（包括清样本后）。拒绝发生在owner/mailbox写入前，不受按键位、坐标、消息间隔绕过。真实Mouse/TouchPad、显式Down、新Pen输入、API缺失/失败和其它未知/应用注入沿原路径；独立Win32Mouse producer、TouchPan、实际墨迹、配置/UI和shader不变。

用户现场消息和GetCursorInfo flags未采集。当前验证证明该错误接管路径被封堵，未把约1秒认定为固定计时器或现场唯一根因；Windows 11设备复测与原Win7硬件视觉验收仍待完成。

## 本轮实际验证

- 独立Trellis check通过，无阻断问题，未修改产品/测试源码；详见check-pen-mouse-takeover.md。
- 完整InkeysRepo.sln Debug|ARM64构建退出0。动态定位本机ARM64原生MSBuild，同一PowerShell invocation规范化PATH、禁node reuse；/m:1 /nr:false，600秒上限。已编译本次产品filter、Window接线与headless新增用例。
- 复用前轮确认的增量链接环境处置：先核绝对路径，仅将Build/ARM64/Debug/Inkeys.ilk保留到本轮ignored日志目录，再用原配置完整链接；本轮未触发LNK1000，也未改项目/工具链/依赖。构建由受限环境外的已授权runner完成，避免已知PDB环境限制。
- 构建stderr为空。保留既有C4267/C4305、未改动Controller的C4834、对齐C4324等warning，以及构建后pwsh.exe查找提示；本轮无源码/链接错误，未为这些提示扩改源码或环境配置。
- 新生成的InkeysHeadlessTests.exe --no-window退出0（300秒上限，后台隐藏执行）。Config metadata为0 failures，最终PASS animation correctness，stderr为空。RunDraw3ContactInputTests实际注册并执行新增TestSystemPenMoveSequence。
- 新序列直接调用生产filter、owner resolver、mailbox、visual resolver与系统显隐策略，对照System/Drawing：旧Mouse→Pen Hover/Contact→清Pen/重复清除→约1秒/长间隔/重复/异坐标/按键位系统Move不改owner和Mouse序列；清Pen后普通笔/荧光笔均无主视觉；真实Mouse/TouchPad/Down以及既有API失败/Win7/注入回退可立即恢复，Drawing明确恢复PenIcon；新Pen可再次接管。测试中的tick为数据，不等待/模拟产品计时器。
- Window模块不在该headless组合中：实际Window调用点由独立静态审查及完整产品编译核验，未将纯组合测试宣称为真实WndProc派发或硬件验收。
- 三个产品文件仍UTF-8 BOM/CRLF，测试仍UTF-8无BOM/CRLF；任务/spec的原格式保持，JSON/JSONL与diff --check通过。构建前后冻结的四源码SHA一致；当前产品与headless产物SHA均不同于基线，排除旧EXE。
- 正式PS与Win7 guarded header的SHA对本轮baseline完全不变，没有重新生成shader/guarded或重复GPU资源验证。配置/UI/i18n与工具投影未改，本轮不重复状态/pen-width生产CLI或i18n生成。

未启动产品主窗口、设置、GUI、Computer Use、HWND/GPU harness，未运行Windows 11/Win7实际设备验收。保留原Trellis任务in_progress并单列待设备验证，不将旧轮结果代替本轮证据。

## 记录与产物

本轮ignored证据目录为Build/verification/pen-mouse-takeover-20261019/：baseline/、baseline-hashes.json、previous-artifacts.json、source-freeze.json、static-check.json、Inkeys.prebuild-incremental.ilk、build/headless stdout/stderr、两个result.json、current-artifacts.json。实施分析见implementation-pen-mouse-takeover.md，独立审查见check-pen-mouse-takeover.md。

| 产物 | 本轮SHA256 |
| --- | --- |
| Build/ARM64/Debug/Inkeys.exe | 87E9E1D64569F2A88811BB0E69F880BC1D1827FE67921912E4FA4CE455DD9AA0 |
| Build/ARM64/Debug/InkeysHeadlessTests.exe | E42551B0D6BA9959F834D9C813A2C7600306845DB4B3B43B7499363C11FFFA39 |
| 正式PS源（未变） | 321D2E9B75F12ABB68359BA4080759410066FDDA14AFB54002518CA970ECAF42 |
| Win7 guarded header（未变） | 480F7B970BD4EE0775902D1ADD3B245A87EF29C5EE35043EEAF41806EF106DBD |

2026-10-19 用户反馈“人工验证通过”，并授权提交本轮修复、结束任务及提交收尾记录。按该验收归档；此前未运行的Win7实机记录保持事实，不推定为Win7实测通过。本次不push。
