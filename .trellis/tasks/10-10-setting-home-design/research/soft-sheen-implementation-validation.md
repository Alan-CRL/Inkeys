# 第四轮主会话验收：克制背景与文案反光

2026-10-11；当前工作区 fluent 分支，继续10-10-setting-home-design。soft-sheen-revision.md为本轮合同，历史强蓝波纹记录保留为历史。

## 最终行为

- 已确认的k圆头共用截面/宽度/方向修复完全保留；字标只保留真实鼠标视差，开场时原彩虹RGB和透明AA不调制。取消开场时间缓存键及无用径向数据，稳定字标不因入口时钟重算。
- 整个圆角渐变背景仍沿用900 ms向外推进，改成中性薄柔光，峰值浅8%/深10%，不再有强青蓝环；仅在字标Z序下方绘制。
- 横幅内下方标语、开源标题和托管说明按实际文本片段中心轻微提亮，最多浅8%/深12%；保持字号、布局、UV和alpha。链接hover/press语义色优先；Copyright及按钮标签/Fluent状态不参与。
- 入口/隐藏时钟、动画开关及速率、离屏门禁、触笔/拖动/失焦回正保持。静止/结束时提前跳过文案径向计算；无新shader、纹理、设备、线程、定时器或额外渲染请求。

## 实际编译与回归

- 完整InkeysRepo.sln Debug|ARM64：ARM64原生MSBuild，同一PowerShell invocation应用PATH workaround，1800秒deadline；退出0，71.850秒，0 error，56 warning。warnings在已有依赖头等路径，本轮五个产品/测试文件未见warning；未清理无关代码。
- InkeysHeadlessTests.exe --no-window：退出0，190.187秒，stderr 0字节，末尾PASS animation correctness。没有启动交互式GUI或Computer Use。
- i18n check：en-US、zh-TW均384/384，新增/缺失/空翻译问题为0，本轮无文案源变更，无需重复sync。
- 保留三语言/导航展开收起/浅深/720×520、960×700、1440×900及额外窄宽场景、七倍率真实字体测量与DrawData回归；HomeBrandDesign在1倍率2032帧/144次实际linkTap，其余六倍率各480帧。链接/按钮tap、Tab/Enter/Space、拖动取消、滚动、隐藏ShowSerial/返回主页/重复当前页、动画开关/速率与离屏门禁均包含在整套通过结果中。
- 本轮新增开场期间缓存不重建、真实按钮命中矩形固定；旧强笔画变化断言改为隔离字标RGBA精确一致，不通过文本矩形背景变化冒充真实字形。

## 真正WARP像素与视觉复核

Windows11 ARM64无窗口WARP明确请求FL11.0，使用生产RenderHome、真实字体、imgui_impl_dx11及CSO资源。浅/深静态基线分别21/23帧建立；冷首帧clip不可见不消费动画或创建字标缓存。

- k圆头浅/深×1、1.875、4三倍率：每主题实体接合采样153/546/2501，六组gaps均0；未放宽alpha阈值。透明字标实体23207像素、部分AA2403、下伸1314、包络外0。
- 字标提取真实生产DrawData顶点/索引进行独立透明绘制。浅/深开场早/晚4次与各自静态RGBA完全相等，保留RGB及alpha；不能将字标布局框内背景变化称为字标颜色变化。
- 文案仅提取真实字体UV三角形，排除白像素、烘焙线段和图标。浅色2528/深色2438像素有小幅RGB变化，最大channel delta14/8；alpha变化0，顶点alpha、pos/UV及三角形数量稳定。
- 排除实际字标与文字alpha后，浅色早/晚背景最大channel delta2/3，深色22/22；有变化背景像素浅83316/38686、深112349/60748。圆角横幅外变化0；结束/关闭精确回到原静态像素。最大值是此配置样本，不是任何显示器上的可见度保证。
- 主会话查看soft-light-middle、soft-light-late、soft-dark-middle真实PNG：浅色很轻、深色可见中性柔带，没有原强蓝光环，彩虹与正文清楚。PNG仅将RGBA8读回无损解码，未做视觉加工；有限网格仍是插值近似，不声称像素级解析柔光。

## 性能（同机同配置真实主页）

每模式预热20帧、采样100帧；Debug ARM64，真实DrawData和完整WARP提交+copy/Map等待。单位ms。

| 模式 | CPU median | CPU p95 | 完成提交/读回 median | 完成提交/读回 p95 |
| --- | ---: | ---: | ---: | ---: |
| static | 1.8647 | 2.2788 | 8.4001 | 10.4494 |
| idle | 1.7758 | 2.2139 | 8.6016 | 10.8554 |
| moving | 2.4620 | 3.2285 | 9.3159 | 12.0524 |
| returning | 1.9888 | 3.6501 | 6.2519 | 12.2914 |
| entrance | 3.2092 | 6.4238 | 14.4409 | 69.8309 |

- 静态/静止/移动/回正13807顶点、57942索引、6批次；开场16645顶点、74322索引、仍6批次，仅增加有限网格2838顶点/16380索引。
- 五模式预热后ImGui分配、字标缓存增长、背景缓存增长均0。字标cache-rebuilds依次0/0/100/200/0；入口时钟不再导致重建。ImGui allocator计数不代表全页std::string/vector或驱动分配，不声称整页零分配。
- 开场完成提交/读回p95为69.8309ms，存在明显尾部波动，CPU与WARP读回不得相加推断原生窗口帧率。本轮不宣称原生窗口60fps、较旧版本更快或Win7实机性能已验证。

## 格式、审查与提交边界

trellis-check独立静态/实际证据审查见soft-sheen-check-review.md；native-desktop主页合同已同步第四轮，当前缺失trellis-update-spec技能按workflow Phase3.3手动审核并记录，不假称技能调用。

最终编码/BOM/CRLF、末尾换行和两份diff --check由audit-soft.py核验；基于HEAD或新增文件原index checkout比较格式，并以SHA256核验原39文件暂存内容未变。最终审计输出encoding-audit-soft-final.json。

用户授权提交此前主页和k修复。实际git commit -m feat(settings): redesign home branding and fix logo caps沿用原1Password SSH签名，返回failed to fill whole buffer，Git fatal: failed to write commit object；没有生成commit，HEAD仍c8f2679190612cab4d24ce6d0e3882c6bd2bc44a。按仓库要求停止该尝试、不重试、不关闭或绕过签名。原暂存快照保留，本轮新改动仍未暂存，没有push。

Win7 SP1+KB2670838仅静态兼容审计：沿用FL11.0管线，不依赖FL11.1接口；Win7实机与原生窗口最终观感仍为人工验收。任务保持in_progress供审阅，没有finish/archive。

## 证据路径

- Inkeys/Cache/home-design-20261010/soft-build-first-result.json、soft-build-first.log
- Inkeys/Cache/home-design-20261010/soft-headless-first-result.json、soft-headless-first.stdout/.stderr
- Inkeys/Cache/home-design-20261010/soft-*.rgba/.png（保持原ripple-*历史证据）
- Inkeys/Cache/home-design-20261010/staged-home-baseline.json、encoding-audit-soft-final.json
