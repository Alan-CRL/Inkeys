# Check review

日期：2026-10-24。按trellis-check完成本任务全部改动审查；未运行GUI、Computer Use、完整构建或耗时回归，没有commit/push。完整构建与新产物运行由主会话执行。

## Findings (fixed)

主会话完整编译后暴露新增离屏测试C2248：直接调用BarUIRendering受保护的GetThicknessFineDialLabelLayout。已在测试块内新增派生FontTestRenderer，用public using公开继承构造函数和该方法，仅修改测试；生产API、friend与module边界保持原样。首轮静态审查未捕获此访问控制错误，实际编译补齐了验证。

## Findings (not fixed)

未发现需要修复的问题或待定产品决策。

## 审查证据

- `Bar.FontSize.h`使用`floor(fontSize*pixelsPerUnit)/pixelsPerUnit`，开启保留原字号；拒绝无效输入、0px与浮点越界，不round或钳为1px。DWrite为FLOAT，回归以合理浮点容差验证物理em高度。
- `BarUIRendering::ResolveFontSize`合并当前DC的Y基向量与DIP模式DPI比例，PIXELS模式不重复乘DPI。Word在frameZoom/contentScale合并后调用；ButtonDrawTransform、底栏body/popup变换均已进入DC。
- 实际文字入口共有共享Word、细刻度DrawTextLayout、dock指示DrawTextW、FPS DrawTextW，全部接入策略。Word/细刻度/dock/FPS的0px结果均跳过，不把空格式传给D2D。MeasureText及dock预先测量保留原上界，符合最终设计。
- 细刻度缓存包含最终DWrite字号，策略变化在render owner清理；同value/zoom但DC缩放变化不会复用旧字号。固定64项缓存保持有界。
- 主栏策略变化发生在帧首，设置unclassifiedDamagePending与visual retry；ResolveDamage读取该标志，仅成功呈现后清除。分页Scene在相同frameTime早退之前比较策略并FullDamage；NotifyLayoutChanged递增revision并轻请求四分页。隐藏恢复及失败重试不会只更新状态而遗漏绘制。
- 新配置归属既有main.json UI3实验容器，IdtAtomic<bool>默认false/NoUpload，复用严格bool codec及复制/重置/Mini；传统deploy.json旧UI3路由清理不影响新main.json字段。
- Setting只新增字段/卡片、现有轻请求与FIFO保存，父panel增加110DIP卡片+5DIP间距。未修改字体图集、字体尺度或设置文字绘制；三语标题/说明与生成物同步。标题长度和卡片说明换行宽度均在既有可用区域内；未将静态核对称为GUI观感验收。
- 数值回归使用同源生产helper；真实Config测试覆盖默认、缺失、非法类型、双向写盘/新实例读取、Mini隔离、冻结副本、重置及NoUpload。
- 离屏BGRA对照实际调用生产Word，与独立推导字号的未量化参考绘制比较；fractional图像不同且切回相同，可捕捉漏接量化/缓存失效。细刻度直接检查生产DWrite layout字号和外层变换缓存。Scene回归先消费SetOpacity的几何damage，再以相同frameTime只改变策略，避免测试被已有dirty或动画遮蔽。
- 最小diff未涉及项目/solution、工具链或第三方代码。新增header通过现有源码include编译，不需要新增构建节点。

## Verification

| 检查 | 结果 |
| --- | --- |
| `git diff --ignore-submodules=all --check` | 通过，exit 0 |
| `pwsh -NoProfile -File Scripts/i18n.ps1 check` | 通过，en-US/zh-TW各437/437 |
| 编码/BOM/EOL | 15个tracked改动与`git cat-file --filters HEAD:<path>`对照均保持；新增header UTF-8无BOM/CRLF |
| Type-check / 编译 | 由主会话完整Debug|ARM64 solution执行，本审查未代称通过 |
| Headless / 字体离屏运行 | 由主会话执行新产物，本审查未运行 |

源码已稳定；如主会话编译或回归暴露问题，应按首个有意义错误定位，不用改工程/环境配置绕过。

## 编译后补充

主会话报告首次真实构建在Bar.EraserAttribute.Test.cpp新增测试处失败C2248；产品源码已编译。本代理按首错实施上述测试局部修复，保留原UTF-8/CRLF，并重新通过diff静态检查。后续完整复编、Headless及离屏运行仍由主会话执行，本记录不将修复后编译宣称为已通过。
