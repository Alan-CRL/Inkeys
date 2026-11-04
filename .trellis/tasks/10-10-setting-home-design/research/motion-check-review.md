# 第二轮主页布局与动效独立审查

审查依据：follow-up-plan.md、check.jsonl 所列 native-desktop 规范、PRD/design/implement，以及实际生产渲染、设置会话、国际化脚本和无窗口测试路径。本记录仅覆盖本次主页布局与轻量动效修订；第二轮批准方案覆盖首轮静态字标、独立开源卡和保留教程入口的冲突约定。

## Findings (fixed)

- File: InkeysHeadlessTests/setting_design_tests.cpp
- Issue: 触屏/笔回正测试把输入放在字标中心，中心本身就是零目标；即使移除 MouseSource 门禁也可能通过。连续测第二种来源时还缺少重新建立偏转的前置条件。
- Fix: 每种来源先使用真实鼠标在非零边缘目标建立偏转，并断言实际 x > 0.85；随后在相邻 2 DIP 的非零边缘位置输入 TouchScreen/Pen，明确断言 io.MouseSource 已切换，再断言归零。复验曾发现同坐标事件会被 ImGui 去重，AddMouseSourceEvent 仅标记下一位置/按键事件；因此鼠标和非鼠标切换均使用真实跨像素位移，不直接写 IO 状态。保留原输入队列和真实 RenderHome 路径，补充中文注释，原 UTF-8 无 BOM、CRLF 保持。

- File: InkeysHeadlessTests/setting_design_tests.cpp
- Issue: 首轮 --no-window 在 1x/1.875x 报两处离屏断言失败。旧测试固定 800 DIP 高度并把请求 scroll=1200 当作全离屏，但实际滚动会被布局重排后的 ScrollMax 夹住，尚未验证字标包络已经退出裁剪框。
- Fix: 记录原视口的实际 scroll、ScrollMax、字标包络底边和 clip 顶边，再使用 300 DIP 视口建立可实现的真实祖先滚动，严格断言完整包络在 clip 顶边外。保留原缓存/反光进度冻结断言及回到 800 DIP 后不重播断言；没有改产品 cull 或放宽要求。最终日志证实：1x 原视口 scroll=max=331、包络底 52.2265 > clip 顶 16，确实尚未离屏；300 DIP 后 scroll=max=889、包络底 -505.774 < 16。1.875x 原视口 scroll=max=617、包络底 101.55 > 30；缩小后 scroll=1200 <= max=1657、包络底 -481.45 < 30。所有离屏及恢复断言通过。

- File: InkeysHeadlessTests/setting_home_warp_tests.cpp（WARP helper owner 修复，本 reviewer 只读复核）
- Issue: 首轮未直接覆盖未预热 AutoResizeY 的首次开场；深色像素基线只等 4 帧，原 Fluent 控件 0.083 s 颜色过渡尚在推进，导致 8 处非字标像素/还原断言失败。
- Fix: 新增 CheckColdEntry，在第一帧直接开启动效，使用真实 pageClip 验证不可见帧不推进、首次可见按 delta 推进一次；StableBaseline 在至少 20 帧后取得连续 3 张完全一致的真实 WARP 读回作为主题基线。所有字标外变化必须为零、反光只能提亮和结束像素精确还原的断言保留，未放宽。已逐项只读审查，最终运行通过：冷启动 clipped=1、visible=11、entries=1、elapsed=0.183333；浅/深主题分别在 21/23 帧达到快照稳定。两主题反光和全部四角视差 outside 均为 0，结束与禁用后精确恢复静态像素。

## Findings (not fixed)

- 产品逻辑未发现需要修复的明确缺陷。三语言标语按相同 14 DIP 字体测量/绘制；标题使用同一 16 DIP 字体和 22 DIP 行框；居中横幅内容、分割线、并排/窄宽上下按钮、版权间距与删除旧入口匹配批准方案。
- Win7 SP1 + KB2670838 实机、原生 HWND/DX11 最终观感和实机触摸未执行，属于用户要求保留的人工验收边界，不把源码/无窗口 WARP 证据标成 Win7 实机通过。

## 审查证据

- HomeMotionState 由设置 resident coroutine 独占；ShowSerial 在现有 settingStateMutex 内读写；真正目标切换和隐藏到显示序号触发进入，离页立即取消。字体重建、设备 epoch、尺寸和主题路径保留 resident coroutine，不以帧缺席重播。
- 现有 settingNavigationClockIdle 在跳过渲染、遮挡、资源失败和恢复路径重置，主页使用相同导航 delta，并应用已有 Enable/SpeedRate。离屏早退在进度推进之前，再次可见不改变 entry 身份。
- 鼠标门禁检查真实 ImGui MouseSource、AppFocusLost、HWND active、内容覆盖层、鼠标按住、活动控件及弹层；Win32 后端从原始消息 extra info 投递 AddMouseSourceEvent，输入来源没有在业务转发中丢失。
- 原始矢量几何只构建一次，预缓存归一化位置/径向距离；运动只重写预分配输出顶点，稳定参数复用结果；透明 AA alpha 原样保留，只有字标位置/颜色变化。无新增产品纹理、设备、shader、定时器或渲染请求。
- Star/Sponsor 复用真实 Fluent 控件、稳定 ID、AllowContentDrag 和原动作；装饰只变图标顶点，不动标签和命中框。GPL 和仓库名称仍由原 inline 片段动作返回，经业务异步 Shell 队列处理。
- Scripts/i18n.ps1 的最小修改只允许基准为空时的显式空翻译，缺失、非字符串、普通文本空翻译、源文案变更及 unfinished marker 继续按旧规则处理。实际函数 9-case 回归日志为 PASS；i18n 主会话报告 384/384。
- WARP helper 限定 FL11.0，调用真实生产 DX11 backend、原始嵌入 CSO 与真实字体纹理，在 CPU 假纹理材质化前创建/销毁资源；没有 HWND 或 swap chain。生产 DrawData 像素读回覆盖字标透明 AA、下伸、反光提亮、四角视差与非字标像素固定。
- 性能统计包含 static/idle/moving/returning/entrance，CPU 与 WARP 提交完成耗时分开；后者明确包含 CopyResource/Map 等待。ImGui allocator 计数不覆盖既有 std::string/vector 分配，零分配结论仅针对预热后的动效缓存增长和实测 ImGui 分配，不扩大成整页堆分配为零。

## Verification

- Lint / 静态检查：git diff --check 通过；仓库此 C++ 路径无独立 lint 命令。此次测试与报告 UTF-8 无 BOM、CRLF 检查通过；主会话最终全量格式审计另记录。
- TypeCheck / 编译：PASS。主会话使用 ARM64 原生 MSBuild 构建完整 InkeysRepo.sln Debug|ARM64，最终 motion-build-verified-result.json 退出 0，26.566141 秒，涵盖最后的输入来源 fixture 修复。
- Tests：PASS。完整 InkeysHeadlessTests.exe --no-window 的 motion-headless-verified-result.json 退出 0，156.923193 秒；stderr 为 0 字节，stdout 结尾 PASS animation correctness。真实 pen/touch 来源、冷 AutoResizeY、实际离屏、WARP 透明像素/反光/四角视差及既有主页回归均通过。
- fixture 闭环：首轮 10 处失败（8 深色主题快照尚在过渡、2 旧视口不足以全离屏）和第二次运行 4 处输入来源去重失败均已由实际事件/几何/像素前置条件修复；未改产品逻辑、未削弱严格归零/像素一致断言。
- i18n：主会话 sync/check 后两语言各 384/384，意图空串实际函数 9-case 回归 PASS；审查脚本与文案生成路径一致。
- 性能：五场景各 100 帧，实际主页 14166 顶点、59346 索引、6 批次保持；ImGui 分配和动效缓存扩容均为 0，idle 重算为 0。CPU median/p95（微秒）：static 1598.1/2062.2，idle 1555.6/1986.4，moving 2186.8/2777.6，returning 2297.5/2801.9，entrance 4093/4636.7。完整 WARP 提交/完成/读回耗时见原始日志；这些是本机 Debug 数据，不外推 Win7 实机或 Release 性能。
- 已复核主会话对 rendering-and-ui.md 与 configuration-i18n-and-assets.md 的规范更新，描述与最终实现及验证边界一致。
- 本 reviewer 未重复启动共享产物构建/测试，未启动 GUI、未执行外链、未使用 Computer Use、未 commit/push。未发现尚待修复的本次产品代码问题；原生视觉与 Win7 实机仍属人工验收。
