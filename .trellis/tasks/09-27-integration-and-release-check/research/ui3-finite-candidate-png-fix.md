# UI3有限布局PNG角色最小修补

Active task: .trellis/tasks/09-27-integration-and-release-check

2026-10-01，root唯一实现，独立cleanup_ui3_design_check检查。范围仅Probe.h、RenderLoop.cpp、render_scheduler_tests.cpp；当前原8源已停写。F066现opt-in计量遗漏，先前正确数值P2 Green pid14416不覆盖此caller。

需求/验收：实际ColorSelect12Wheel几何/enable/pct尚活跃时不得settled；隐形且target无关、其它PNG不阻有限布局；不认证PNG像素，不增加Advance/Request/dirty；defaultnull不额外读visibility。原UTF8/BOM/CRLF保持。

设计/执行：真实pngMap按实际enum匹配共用Ui3FinitePngLayoutRole(bool,bool)，RED初值仍Unspecified；B222调用同helper/真实Observer，先让其它六roles已settled，再注入该颜色环active/unequal，应pending；下一frame颜色环settled、其它/隐藏active不阻。真实RED后最小绿色映射，正确project-output验证；主Solution待Draw cpp写入冻结后编译。

状态：RED source已写，standalone build exec24623运行。无新主Solution/GUI/性能结果。独立review禁止据此扩family或判完整首发。

## 真实RED与GREEN源

- B222 RED standalone Build0/strict --no-window自然1 pid23084，stderr仅B222 failed、FAILED count=1，旧B/P/R/216全部通过。raw ui3-b222-png-red-{headless-debug-arm64-build,project-output-debug-arm64-headless}，未将stdout含failures=0误作失败或整套通过。
- GREEN仅共用映射改为真实可见颜色环返回AttributePreview；其余Caller/test/B2逻辑未动。当前尚未GREEN Build/Run，独立checker可开始源码增量。

- Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h SHA256 AD1F8485C3AB5D039AF6D2E5561D5A33189EC8D74A838EF0E80FF1FCCC918B93
- Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp SHA256 408CFEA1A5C7417E4274FD26E8A28061009756CAD627A18C686EF768E96215DC
- InkeysHeadlessTests/render_scheduler_tests.cpp SHA256 F8F66DDF26E47A498319EFBF8353FBA792F4D6DE4E679CD57441452599026FF2

## 首两轮夹具前提错误（保留失败，不作因果RED）

第一RED pid23084和第一次GREEN pid34360均自然1/仅B222。定位实际Publication明确拒DrawAttribute目标flags67（fold+draw同时true）；因而首红不构成角色遗漏的因果red证据，首green1不是产品失败。合法本景应flags66（draw+auxClosed、main展开）。已只修夹具66，并明确Snapshot与两次MarkConsumed成功前置，保留所有角色pending/stationary断言；不放宽业务支持门。映射重新回到原Unspecified RED stub，同合法case重建/重跑后才转GREEN。新的测试前缀ui3-b222-png-valid-red/green，旧两原日志不删除。

## 首次完整编译（合法RED）

完整InkeysRepo.sln Debug|ARM64 c3b-u2p2-b222-valid-red-debug-arm64-build实际1，唯一源码错RenderLoop5709 helper未限定namespace；此函数原仅using Ui3PropertyRole、不在Bar namespace。Root最小限定Inkeys::UI::Bar::Ui3FinitePngLayoutRole，未动helper/测试/角色语义。136warning/1error保留；Headless目标该轮真实产新Root/Build/ARM64/Debug输出，但整体Solution不叫通过。DrawP2/C3B本轮暂无类型错，仍要下一完整0。

## 合法前提下真实RED、GREEN源码再次冻结

完整Solution compilefix真实0；正确Root/Build --no-window ui3-b222-png-valid-red-debug-arm64-headless自然1 pid10952，stderr准确仅B222/FAILED count1。合法66与Snapshot/两次MarkConsumed前置在同一生产observer，旧B/P/R/216通过，才是本次F066因果RED。GREEN仅恢复上述共用role映射，保持合法case/前置/全部断言；当前source冻结，root下一完整Build/Headless待实际结果。

- Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h SHA256 AD1F8485C3AB5D039AF6D2E5561D5A33189EC8D74A838EF0E80FF1FCCC918B93
- Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp SHA256 F59F9A59BEC13C773C3E4CB9A91AC902EE1555F584790D13806D783AEA8228A8
- InkeysHeadlessTests/render_scheduler_tests.cpp SHA256 16327A081799E2780DBBDE6C00B10BA69158AED2EFEC91A2A70E503B2B2E3ACA

## 最终本单元验证与独立review

合法Green全Solution c3b-u2p2-b222-green-debug-arm64-build0，Headless自然0 pid26160；parked22032/PptCOM17700自然0。独立ui3-finite-candidate-code-review.md SHA220E4CAB708FF17CAE6B0F4A71BBC066BED13E407797734B84ECEB5068994D15 GREEN已核actualdiff/全部失败与最后实际0。三源hash维持AD1F/F59F/16327；Root停写并已唯一交接B3 writer，不复用本局部门作SVG/GUI/性能/最终HF。
