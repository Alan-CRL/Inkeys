# Trellis check: 选择与穿透

## 检查范围与状态
- 分支 feature/mode；仅本任务范围内修复，无 commit/push/GUI。
- 已读取原生注入完整输出及 check.jsonl 所列需求/设计/实施与相关规范；检查实际 IdtState/Draw3/Setting FIFO、鼠标输入、Bar 显隐/尺寸/高亮/命中和配置 codec。
- 当前产品源码审查已完成。完整 ARM64 solution 编译与生产/Headless/offscreen 执行由主会话负责，最终结果追加到本任务 validation 记录。

## Findings (fixed)
1. Bar.EraserAttribute.Test.cpp：Set 模式启动标签过渡，content.tar 只在透明中点替换。原测试立即比较目标会误报；改为推进真实过渡后比较，并保留即时显隐/尺寸/状态断言。
2. Bar.Button.cpp/.cppm、Bar.RenderLoop.cpp：英文两行字号原来根据已应用 content.tar 决定，在透明中点替换后仍可能短暂用旧13 DIP；新增局部目标标记，换字前立即缩小，短标签替换前保留9 DIP，内容推进时更新字号目标。加入实际 DWrite 过渡中点前后测量与短标签恢复断言，保留网格。
3. Bar.RenderLoop.cpp、Bar.RecallAttribute.cpp、Bar.RecallChrome.cppm：旧 Recall 使用固定 C8C8C8/完全不透明，Clean/Freeze 使用主题/0.30，违背四类不可用统一效果。主栏所有尺寸与弹窗共用主题 TextPrimary 和 BarButtonDisabledContentOpacity；删除旧色解析。命令可用性、灰色重复点击保护、3秒期限、几何均未改变。
4. recall_attribute_tests.cpp：用真实透明度动画替换旧固定灰色测试，覆盖中间帧、反向连续和重复目标收敛；检查生产弹窗接入共同禁用视觉 helper。
5. configuration-i18n-and-assets.md、rendering-and-ui.md、draw3-integration.md：同步统一禁用样式、英文换字字号、穿透工具身份/运行时注册、选择文案、电脑操作 owner 和 backend 投影，移除旧“恒为选择/无Pierce/全部非Selection绘制”合同。

## Findings (not fixed)
- 未发现需遗留的当前任务代码问题。白板功能入口当前 IsWhiteboardFeatureEnabled=false；按主会话决策保留原白板事务与无 Pierce 投影，不扩大修改。如果未来开放入口，应核对白板从独立穿透工具进入时的 Move 高亮映射；这不作为当前公开功能故障。
- 真实鼠标/触摸穿透、PPT 运行期和设置最终视觉属于人工验收，本次未授权 GUI，未执行；无窗口检查不能代替这些结果。

## Verification
- Lint/静态：git diff --ignore-submodules=all --check 通过；Scripts/i18n.ps1 check 两语言均349/349、100%通过。
- 编码/换行：局部修复文件保留原 CRLF，Bar.EraserAttribute.Test.cpp 保留 UTF-8 BOM；无大面积格式 diff。
- TypeCheck：由主会话完整 InkeysRepo.sln Debug|ARM64 编译提供，执行中待最终记录。
- Tests：生产模式 CLI、Headless --no-window 和 DWrite offscreen 由主会话执行，待最终记录。
## 实际运行后的验证修复
- 主会话完整 Debug|ARM64 Solution 构建已 exit 0；正常权限 Headless --no-window 已 exit 0（Config metadata 0 failures、A2/Recall/Eraser 等通过）。运行环境权限失败不通过源码或工程配置绕过。
- --state-mode-production-test 的 whiteboard-request-rejects-clickthrough 曾失败：IsWhiteboardFeatureEnabled=false 时 RequestWhiteboardActive(true) 真实请求被拒绝，不存在白板事务。测试改为验证关闭门禁保留桌面穿透；功能开启时仍验证请求阻止穿透。未改变白板产品开关。
- --bar-eraser-offscreen-test 曾触发访问违规：实际 owner 已是全局 barUISet，问题是新 Pen 状态 UpdateRendering→ThicknessDisplayUpdate 访问 fixture 未创建的 DrawAttributeBar_ThicknessDisplay。仅测试中创建该 Word 对象后才设置 content updates ready；没有新增产品注入/API，也没有创建 HWND。
- 离屏日志目录在打开报告前创建；多语言原标签恢复时同时恢复普通字号，避免新增两行测量污染既有截图。
- 本轮只改 IdtState.cpp 内生产测试和 Bar.EraserAttribute.Test.cpp；diff --check 通过，原编码/换行保留。等待主会话必要增量构建及这两个 CLI 复核；未改的 Headless 全套无需重复。

## 最终复核
- 必要增量编译及完整 ARM64 链接 exit 0，0 errors（solution-final-fixtures.log）。
- 修正夹具后两个 CLI 均通过：StateModeProduction exit 0；Bar offscreen exit 0，EraserVisual failures=0。
- 最终29个跟踪文件编码/BOM/CRLF核对和 diff --check 通过。完整自动结果、环境参数和人工待验项见 ../validation.md。
