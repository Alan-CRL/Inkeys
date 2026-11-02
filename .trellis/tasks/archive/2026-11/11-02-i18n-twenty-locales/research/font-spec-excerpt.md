# 最终上下文摘录

来源：`.trellis/spec/native-desktop/rendering-and-ui.md`，2026-11-02本任务最终工作树。后续原规范变化时重新读取对应章节。

## Setting FreeType 动态字体与单通道图集（2026-11-02）

### 1. Scope / Trigger

设置窗口字体、字体DPI/图集、语言切换资源或ImGui DX11 backend修改适用。固定ImGui1.92.7；FreeType只用于Setting文字。Bar/PPT/白板分页继续DirectWrite，MessageBox继续GDI+，不能从全界面语言刷新需求推导为统一替换渲染器。

### 2. Signatures

- `Design::ConfigureFreeTypeAtlas(ImFontAtlas&)`：atlas级FreeType loader及当前灰度Alpha8像素格式。
- `Design::FreeTypeTextFontConfig(notoSource, TextFontRole, notoPrimary)`：真实字族/角色校准、LightHinting、源RasterizerDensity=1；动态字号已包含窗口倍率。Setting启用项目SnapAscentToPixelGrid标志，按baked RasterizerDensity将主脸ascent对齐物理像素；不改变em、advance、字形位图或行框。
- `ImGuiFreeType::HasFontLoadFailure(ImFontAtlas*)`：本地维护的错误锁存接口；其他loader安全返回false。
- `IDR_SETTING_IMGUI_ALPHA_PS = 322`：`imgui_alpha_ps.hlsl/.cso`；原RGBA PS继续用于普通图片。
- Debug无窗口入口：`--setting-font-lifecycle-test`、Headless `--setting-font-benchmark-only`及真实Setting页面/控件/WARP回归。

### 3. Contracts

- 字族和regular/bold资源不变，保留Legacy/Design角色与语义行框。图标仍显式使用stb源配置及原密度；FT不解释Oversample。源尺度与主脸基线分开，不能删掉原Noto/Harmony校准而改变字面大小。
- 当前字体采用灰度模式，即便字体文件含COLR/CPAL/SVG表也不能称文件没有彩色能力。若将来启用atlas或任一source的LoadColor，必须显式选择RGBA图集并验证，不得把彩色覆盖压成单通道。
- CPU图集使用Alpha8；支持设备按R8_UNORM上传，专用PS还原白RGB和coverage.r作为alpha。设备能力不满足时保留Alpha8展开RGBA的回退。RGBA图片、自定义shader、混合/采样规则不能随文字改动全局变化。
- backend以自己持有的实际SRV注册信息区分R8/RGBA，缓存DrawData可能只存裸TexID，不能只看TexRef._TexData。创建完整成功后登记，释放SRV前移除登记；更新使用实际GPU格式的row pitch和dirty rect。回退暂存分配失败返回E_OUTOFMEMORY，不消费待重试事务。
- 普通绘制按实际纹理选择默认PS；用户callback之后查询实际绑定PS，属于backend的RGBA/R8默认PS时继续自动选择，外部PS保持接管。ResetRenderState恢复默认状态。无操作/只改sampler的callback不能导致后续RGBA图片误用R8 shader；退出后恢复调用前的D3D状态。
- **ImGui1.92纹理引用使用`texture.GetTexRef()`。不得写`ImTextureRef(&texture)`**：后者会走兼容void*构造，把描述对象地址当成GPU句柄，可能在PSSetShaderResources访问异常。
- 字体替换只在安全帧边界：注册候选atlas及OwnerContext、创建默认bake/初始纹理，准备就绪才提交语言；旧backend纹理在metadata有效时释放，撤销PlatformIO队列/注册后才能删除atlas。更新default/current/ImFluent绑定并撤销旧字体截图和提交缓存，保留ImGui context与导航/编辑状态。
- 相同字族可复用atlas。候选失败恢复旧绑定；已提交atlas后续FT错误或纹理上传失败不Present该帧。FT错误不能清锁存后直接重试缺字缓存，须在安全边界创建新atlas。
- 本地FreeType补丁保留同atlas同buffer/size/FontNo的FT_Face引用共享、独立且按需的merged FT_Size、每次glyph请求激活正确size及失败清理。原始font/charmap仍提前验证；不保留实验性的lazy-face/cmap覆盖缓存。来源与补丁清单见loader目录UPSTREAM.md。

### 4. Validation & Error Matrix

| 场景 | 必须行为 |
| --- | --- |
| 更换语言/倍率时旧字体仍被上一帧引用 | 等安全边界替换；不悬空ImFontBaked/UV/TextureData |
| 候选初始创建/上传或语言revision校验失败 | 释放候选，旧语言及绑定可继续绘制 |
| FT size/glyph加载失败 | 错误锁存、拒绝失败帧，后续新atlas恢复 |
| R8不支持或强制回退测试 | RGBA展开输出等价，相关能力与内存结果分开记录 |
| 带非零offset的多个dirty rect | 按真实格式/stride上传，未更新区域保留，失败后可完整重试 |
| R8文字→noop callback→RGBA图片 | 自动PS选择仍生效；外部自定义PS则保持到reset/显式恢复 |

### 5. Good / Base / Bad Cases

- Good：相同Alpha8数据的RGBA回退与R8路径实际GPU像素完全一致；在完整页面预热语言下拉、基本页、主页后资源仍满足预算。
- Base：同字族只刷新文本及缓存，现有DWrite/GDI+路径各自继续工作。
- Bad：只换io.Fonts而忽略注册/队列；以TexData指针充当SRV；只测小英文样本或在计量时调用会加载字形的方法，宣称所有实际页面节省内存。

### 6. Tests Required

- 保留独立stb基线，生产FT配置共用；20语言、浅深、整数/分数倍率，字号/基线/墨迹/换行与真实控件裁切。
- 公共校准样本保持原严格行框阈值；新增特征字母另查覆盖和实际字号。语义行框不是最终scissor，重音overhang须经真实Caption控件完整字图UV/尺寸、行区域和引用该字图的ImDrawCmd::ClipRect验证；不能只据字形超出16DIP行框就缩字或移全局基线。
- 实际四角色+Legacy图标+标题栏字体集、真实Combo打开/滚动/关闭、页面往返后测量。统计ImGui/FT requested live allocations与全部存活GPU纹理；CPU图集是ImGui分配子集，不能重复相加。GPU以实际resource desc计R8/RGBA字节，WARP不是独立显存；进程/驱动额外开销不伪称已精确计量。
- 指标采样只能查询已有bake/glyph，不用FindGlyphNoFallback等按需加载API污染被测工作负载。冷字体注册/Build与冷首帧、暖态median/p95分别报告。
- 真WARP创建/偏移更新/回退分配失败重试；相同Alpha8数据RGBA↔R8全图像素一致，图片/AA线/noop/外部PS/cached raw ID均覆盖。原始RGBA黑透明padding与Alpha8白覆盖差异单列，不能混作R8转换误差。
- 真实生产替换/同字族复用20语言多轮、过期候选和FT/DX11故障，旧context状态保留、新atlas错误恢复、资源不随轮次增长。无GUI结果不替代实机窗口/其他平台验收，具体通过与限制留在任务记录。

### 7. Wrong vs Correct

```cpp
// Wrong：void*兼容构造会把CPU描述对象地址当作GPU纹理句柄。
list->AddImage(ImTextureRef(&image), topLeft, bottomRight);
// Correct：保留动态纹理身份，由backend处理创建/更新后的TexID。
list->AddImage(image.GetTexRef(), topLeft, bottomRight);
```
