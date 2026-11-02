# Noto 设置字体校准独立审查

日期：2026-11-02；基线 e5f0574e。仅审查当前F1–F5后续阶段，不重新解释上一轮679条翻译结论。

## 静态核验

- 产品修改集中于 `Setting.Typography.h` 和 `Setting.cpp`。Harmony Design保留原1.213×0.97及0.75参考偏移；Legacy Harmony单独使用中性配置，不误用Noto补偿。
- Noto JP/KR的Design与Legacy按各自hhea跨度匹配相同角色参考，400/700资源、语义字号、行框、图标、DPI几何与主栏未改变。
- 图片辅助复用既有无HWND DX11/WARP Offscreen；在CPU模拟纹理ID之前运行，COM/WIC和D3D清理有作用域保护，原基准测试默认目标尺寸保持。
- 新测试直接读取真实字体glyph/DrawData并有旧Noto负对照，不是比较实现常量。生产与测试字体选择仍须保持同一family/role及merge规则。

## 首轮独立发现与建议

1. **Merge基线需按primary确定。** ImGui每个DstFont只有primary的baked.Ascent。fallback字面的ExtraSizeScale按自身字体跨度，但GlyphOffset应沿用primary-role基线，否则同一行不同来源会再次错位。主会话/实现者亦独立发现同一问题，正在补共同TextFontConfig与真实SC+KR/Noto+SC缺字测量。
2. **PNG列标题重影是夹具定位问题。** SectionHeader先AdvanceGap，Dummy/ItemSize会将CursorPos.x复位到window/group起点。五列仅绝对SetCursorScreenPos时会在同一起点叠印。应每列独立group/child，不改变产品SectionHeader。
3. **小字号相对误差需排除quad栅格化误判。** 首轮Legacy20四例为19/17px，底部相同，顶部差2px；需要区分实际可见ink、字形设计和bitmap/AA边缘。VertexBounds只取quad；不应因为这一个比例把正确em缩小。建议真实atlas alpha边界或有依据的绝对像素余量，且保留大字号/aggregate旧Noto负对照，防止原约29%尺寸损失漏检。

## 后续候选复核

- 实现已补 `TextFontConfig`，源字族只决定字面尺度，primary字族/角色决定所有merged源的基线；生产加载与测试同用，避免复制错误选择逻辑。
- 已增加真实缺字 `课` 和Hangul在SC/Noto两种primary下的上下边界比较，覆盖14/28/40号、Regular/Strong和100/150/200%。1.25DIP容差在大字可区分旧source自身offset造成的约2px误差。
- 五列改为独立child，PageHeader/SectionHeader使用自己的content起点；描述进一步从SectionHeader真实结束cursor向下排，夹具重影修正不涉及产品控件。
- 尺寸锚改为共同汉字和H/数字，g/q字形设计单独用descender完整性断言覆盖；真实Agpqy仍展示在PNG。共同字样±10%判据保留，只有≤1物理像素的quad栅格化差作为绝对余量，不随DPI放大。跨六字号的旧Noto负对照聚合保留0.85/0.92阈值，仍能抓原约0.71/0.84尺寸回归。
- 独立查看100%真实WARP样张：校准后的日/韩字面与Strong层次恢复，接近SC参照；混合汉字/假名/Hangul大小和同排位置稳定，五列分组标题不再叠印。此为离屏样张检查，不是实际桌面GUI验收。

## 当前验证状态

- 最终产品编译：主会话第三候选完整Debug|ARM64 exit0，12.88秒。
- 最终字体/设置专项：主会话报告 `--setting-general-only --no-window` exit0，10.96秒；100/150/200%全部失败0。
- 旧配置负对照在scale2的Design聚合约0.715/0.719，Legacy约0.843/0.846，证实原缩小回归仍被有效识别。
- 本审查执行 `git diff --check --ignore-submodules` 通过，未发现剩余阻塞源码问题；没有修改产品文件、并行启动编译或GUI。
- 完整 `--no-window` 最终回归：主会话确认exit0，358.22秒，stderr为0字节，末尾 `PASS animation correctness`。产品源码在最终构建后未再修改。
- 未执行交互GUI/Computer Use、commit、push或归档；实际桌面观感由用户试用，离屏证据不冒作GUI实机验收。

## 最终结论

当前F1–F5范围没有剩余阻塞源码问题。家族/角色分离及merged基线修复、真实字体下界与负对照、三DPI样张、既有十一语设置回归均已取得自动验证证据。没有扩大主栏、字体资产、字重、全局几何或Harmony日文候选范围。
