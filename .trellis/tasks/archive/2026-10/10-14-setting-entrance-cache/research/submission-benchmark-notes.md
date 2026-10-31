# 第二轮提交/消融测试实现说明

2026-10-14，benchmark实现代理只修改 setting_home_warp_tests.cpp；核心helper/产品状态由submission_core持有。未执行build/run/GUI/Computer Use/commit/push，主会话统一验证。

## 真实生产接线

Offscreen使用 FrameSubmissionCache/FrameSubmissionKey/RequiresHomeEntrySubmission，与产品相同在Clear之前做精确比较；输入Trail、backend/removed失败和所有准备/捕获/恢复强制实际提交。键纳入真实clear、材质/geometry/font/epoch、target、show和GPU页面cache Revision。只有实际成功EVENT完成或正确性Map完成才ConfirmPresented；skip不发EVENT、不Clear、不copy、不upload、不Draw、不模拟Present成功，也不调用CompleteHomeEntryFrame虚增准备。正常CPU Frame/Continue/entry clock继续。

正确性用独立Readback；只用于测试比较保留target像素。无限制性能样本完全无staging Copy/Map、Sleep、Present，每个真实提交用EVENT+Flush及10秒有界GetData确认。提交快照每帧比较CPU墙钟单列；Capacity是生产helper预算，不是新增GPU内存。

## 新增有意义回归

- 每个浅深主题：真实静态主页第一次提交+12次相同帧skip，整帧精确RGBA，实际clear只1次。
- Hold十次skip仍推进约200ms原时钟；首个渐退归位立即实际提交；终态前所有真实布局恢复帧强制提交，最终hero不裁切/截图释放。
- 实际ResetRenderState callback即使RGBA相同仍运行；独立真实纹理WantCreate/WantUpdates/WantDestroy都不能skip；IndexMap故障撤销快照，相同下一帧真实重试。
- 实际Fluent hover颜色和SetItemTooltip延迟；无后续mouse事件仍出现并实际提交。真实InputText字符事件、1.2s caret可见/隐藏跨150帧、caption及轮询数据文本变化重新提交。
- 既有四材质精确对照新增hold；opaque/style-alpha1省copy，partial-style保留copy。强制copy对照和默认省copy逐像素相同。
- 浅深7材质/边界x4时刻：Solid、Mica、style alpha .6、bg alpha .6、分数像素顶部、partial客户宽度、RGB非bytegrid(2.5/255舍入tie)。省clear候选前先将整目标污染为非预期RGBA，仍须和原Clear整帧精确相等，证明不依赖discard/离屏保留像素；所有不安全条件回退原Clear。
- .76 writing覆盖检查先建立真实cache再回拨时间，仅验证覆盖/像素安全，不用于真实writing生产吞吐；已有mode4基准仍无提前cache。
- 独立分解真正生产foreground：首4vtx/6idx是矩形cover，其余是原字标geometry/UV/colors/indices；hold/dock/settle分别先绘cover再绘ink，必须整帧精确等于原一次混合。

## 固定同帧消融与输出解释

新增WARP固定hold=1.6/dock=2.16/settle=2.54，各4组：完整cache合成、去字标、去蒙版、仅cache copy+title/root。全部保留正常Clear与真实cachecopy/标题，hold也强制copy以保证去蒙版组底层像素定义完整。直接选择原网格范围，不重新采样或降低精度；CPU仍生成原页面快路径与字标后切范围，CPU时间包含切分开销，不能当作生产删除字标后的CPU收益。

另增加hold默认省copy与强制copy控制，3阶段Solid正常clear与候选省clear控制，static/hold正常精确duplicate skip。每组20预热+100采样，同字体/同设备/1000x1100。输出actual-submits/skips/clears/copies/captures、snapshot字节/固定容量、geometry及CPU/compare/backend/wait/total median/p95。实际render组输出无限制render FPS，skip组只标CPU callback rate，不输出显示FPS。既有HomeWARP benchmark 39组继续是强制实际draw控制，不能混淆正常重复帧skip收益；硬件诊断沿既有独立设备，不新增设备组。

限制：实际完整Setting协程/原生caption/业务图片/其他共享客户端/Present不在fixture内；caption/data变化用真实ImGui文本和Fluent控件模拟相同绘制字节变化，非原生HWND。无需GUI。主会话已执行本轮完整suite：exit0、238.33s、stderr0。随后按配对结果撤回生产省Clear候选；最终版本已由主会话重新完整build/test：Debug|ARM64 build exit0/22.3754s，全套--no-window exit0/278.4643s、stderr0，60组完成；最终报告见../submission-validation.md。

## 清屏候选撤回（实测后）

三个固定阶段的省Clear候选收益分别约+0.69%/-0.80%/+0.81%，没有稳定收益，产品接线与公开门禁已撤回。fixture局部PrepareExperimentalHomeEntryTargetClear保留原byte-grid/整像素/完整覆盖门禁；标准生产Filter之后仅实验插回已合法生成的background首层，使用正常AddDrawList更新几何及列表计数。候选组输出clear-policy=experiment-only-not-in-product，全部真实产品提交仍清屏；成功的dedup/opaque省copy保持。实验不走duplicate路径、不需要生产key.clearOmitted字段。
