# 第四轮独立审查：克制背景与文案反光

2026-10-11；trellis-check；Active task: .trellis/tasks/10-10-setting-home-design。读取保存的 native hook、check.jsonl 七项上下文、PRD/design/implement 和当前首页规范尾部；soft-sheen-revision.md 优先于历史强波纹约定。

## Findings (fixed)

未发现需要局部修复的产品或测试问题；审查未修改五个产品/测试源码文件，未操作 Git 索引，未重复构建或启动测试 EXE。新增本审查记录。

## Findings (not fixed)

未发现未修复的任务内源码问题。

- 提交仍未完成：此前主会话的原 1Password SSH 签名返回 failed to fill whole buffer / fatal: failed to write commit object。这是本机签名环境问题，按仓库规则停止重试，不关闭签名、不绕过；原暂存快照和本轮未暂存边界由主会话保存。
- Win7 SP1＋KB2670838 实机与原生 HWND 最终观感未运行，按本轮明确授权留人工验收。Windows 11 ARM64 的无窗口 FL11.0 WARP 结果不能替代这些证据。
- 开场完成提交/读回 p95=69.8309 ms，记录这一真实尾部波动；包含 copy/Map 等待的测量不能据此承诺原生窗口 60 FPS。没有借此扩大范围改动渲染调度或引入专用 shader。

## 真实代码路径复核

- Setting.HomeBrand.h 的字标 Prepare 仅以宽度/DPI/主题/x/y 为缓存键，输出始终采用原 light/dark 彩虹色。删除开场径向字段、shine/reach 参数与颜色调制，不触碰已确认的 k 共用截面/端头修复。原矢量、抗锯齿和鼠标投影保持。
- DrawRipple 在 HeroBackground 后、DrawHomeBrand 和正文前提交；固定 66×43 网格、2,838 顶点，弱白色波带浅峰值 8% / 深 10%，保留圆角和进入/可见性时钟。未新增设备、纹理、线程、定时器、shader 或帧请求。
- DrawInline 仅在横幅的标语两段、开源标题和托管说明传入 motion。启用且 hero 可见、0 < shineElapsed < .9 时才做片段中心径向计算；带外直接复用原色。正文浅最多 8% / 深 12% 朝白提亮，alpha 和字号/位置/命中不变。GitHub/GitCode hovered 或 active 时先取原 Fluent 语义色并跳过反光。Copyright、探索及按钮标签没有传入该上下文。
- RenderHome 的实际进入/ShowSerial、离页取消、关动画即时稳定、离屏暂停、触笔/拖动/弹层/失焦视差回正均沿用已验证路径。未改变 HomeAction 值和动作路由。
- WARP 测试从真实生产 DrawData 匹配字标缓存位置/颜色并提取全部三角形，独立透明绘制后精确比较 RGBA；真实文字按字体 UV 三角形提取，排除 white atlas 与烘焙线 UV，验证几何/UV/alpha，不使用文本矩形内背景变化代替字形证据。背景变化由静态字标/文字 alpha 掩码排除，另设克制幅度上限。

## Verification

- Lint：项目此范围无独立 C++ lint 命令；独立 git diff --check PASS。独立 Scripts/i18n.ps1 check PASS，en-US / zh-TW 均 384/384。
- TypeCheck：核对主会话完整 InkeysRepo.sln Debug|ARM64 构建结果，ARM64 原生 MSBuild、同 invocation PATH workaround、1800 s deadline；退出 0、71.8501536 s、0 error。56 项 warning 为既有依赖路径，本轮五个产品/测试文件未见 warning；未顺手清理。
- Tests：核对 soft-headless-first-result.json，--no-window 退出 0、190.1869384 s；stderr 0 字节，末尾 PASS animation correctness。真实三语言/七倍率/浅深/导航和窄宽/布局/链接按钮 tap、Tab/Enter/Space、拖动取消、滚动及动效生命周期回归保留。
- 编码：独立检查五个分配文件均严格 UTF-8、无 BOM、BareLF=0、末尾 CRLF；没有大面积换行变更。本记录同为 UTF-8/no BOM/CRLF。跨文件最终 audit-soft.py 与原 index SHA256 核验由主会话在所有记录写完后执行。
- 规范：rendering-and-ui.md 当前首页合同与第四轮产品一致，包括字标颜色不参与、时钟不进入字标缓存键、弱背景＋真实文案、链接交互色优先、稳定径向门禁和分配计数边界；未发现需要额外修改的规范条目。

## 无窗口 WARP 与视觉证据

- k 在浅/深各 1 / 1.875 / 4 倍的接合实体采样 153 / 546 / 2501，六组 gaps=0；透明字标 AA 2,403 像素、下伸 1,314、包络外 0。
- 浅/深早与晚共四次孤立字标 identical-rgba=1；稳定开场字标 cache-rebuilds=0，按钮实际 hit 矩形一致。
- 真实文案浅 2,528 / 深 2,438 像素轻微 RGB 变化，最大通道 delta=14 / 8；alpha 变化 0，geometry/UV 相同。
- 浅背景早/晚最大通道 delta=2 / 3，深=22 / 22，圆角横幅外变化 0；结束及动画关闭回到完整静态像素。
- 独立查看 soft-light-middle、soft-light-late、soft-dark-middle PNG。浅色保留薄荷/淡紫原渐变，无强青蓝环；深色为弱中性柔带，彩虹字标、下伸、正文和按钮清楚。固定网格为插值近似，此配置未发现需修复的锐边或圆角溢出；没有使用 GUI 或 Computer Use。

## 性能记录复核

同机 Debug ARM64，每模式预热 20 / 采样 100 帧；CPU 与完成 WARP＋copy/Map 分开计时，单位 ms。

| 模式 | CPU median / p95 | 完成提交与读回 median / p95 |
| --- | ---: | ---: |
| static | 1.8647 / 2.2788 | 8.4001 / 10.4494 |
| idle | 1.7758 / 2.2139 | 8.6016 / 10.8554 |
| moving | 2.4620 / 3.2285 | 9.3159 / 12.0524 |
| returning | 1.9888 / 3.6501 | 6.2519 / 12.2914 |
| entrance | 3.2092 / 6.4238 | 14.4409 / 69.8309 |

基线 13,807 顶点 / 57,942 索引 / 6 批次，开场 16,645 / 74,322 / 6。五模式预热后 ImGui 分配、字标/背景缓存增长均 0；字标重建 0 / 0 / 100 / 200 / 0。ImGui allocator 不统计全页 std::string/vector 或驱动分配，不宣称整页零分配或实际帧率。主会话 validation 报告与日志数值一致，提交失败和人工验证边界明确。

证据：Inkeys/Cache/home-design-20261010/soft-build-first-result.json / soft-build-first.log；soft-headless-first-result.json / .stdout / .stderr；soft-*.rgba/.png。
