# 真实页面 FreeType 视觉回归

修改 `setting_design_tests.cpp`：主测试字体源切换为生产ConfigureFreeTypeAtlas/FreeTypeTextFontConfig，图标保持显式stb loader与原密度。所有既有控件/导航/主页/长说明布局断言保持原门槛；未为hinting放宽裁切或位置断言。

旧Noto calibration测试仍运行stb，并使用固定cf4d9120的独立配置，不随生产参数改变；仍包含旧未校准Noto负对照。

新增CheckFreeTypeRealPages在完整设置设计测试运行（专项generalOnly不运行该截图矩阵），生成Build/setting-fonts/pages下的配对PNG与page-metrics.csv：
- 实际RenderNavigationPane与Language页面ComboRow、RenderGeneral Basic和RenderHome；不复制控件绘制。
- 11语言、浅深、1/1.5/2倍率；CN/JP/KR/en额外1.25/1.875/4倍率。
- 每组独立context加载两种真实Design字重+fallback与实际stb图标；旧stb和候选FT分别运行，避免切换loader污染基线。
- 图片仅是页面可见viewport；长内容按正常滚动clip，不等于完整内容全部同时可见。已有CPU真实布局测试负责长卡片容纳/换行/交互边界。
- CSV记录第3帧实际内容总extent、400物理倍率宽度说明测高、混合H/g/言/한的glyph quad top/bottom和总advance。数字用于人工比较/定位，不能把字形quad含透明边距当实际墨迹，也不能仅凭数值等同观感。
- CSV遍历全部TexList累计CPU像素内存，并对已有TexID的图集按当前生产DX11 RGBA上传计算GPU逻辑字节。采样在第三帧内容绘制完成、导航及最后backend提交之前，已含前两帧真实上传但不代表最终稳定峰值；不包含FT/ImGui所有分配，不能单独证明整页总内存达标。
- 再次调用SaveSettingFontComparison会重建真实后端，其初始化/恢复逻辑与原夹具一致。截图尚不能证明实际设置窗口的线程/热切换行为。

状态：已交给dynamic_fonts串行构建和运行，尚未获得结果，禁止宣称通过。

更新：已将页面截图推进到120帧预热。SaveSettingFontComparison增加默认兼容的warmFrames/afterSubmit参数，仅最后一帧读回PNG，回调在每帧实际backend提交之后执行。页面在第120帧后遍历全部TexList并检查事务OK/Destroyed，从每个真实SRV读取Texture2D desc统计GPU逻辑字节；CPU像素内存作为ImGui分配子集，不重复计。

每个独立语言/倍率/主题/loader上下文使用独立ImGui与FT requested-allocation计量器，源字体buffer在全部场景间共享且排除同样的公共值。报告ImGui live（含CPU atlas及共同UI/backend对象）、FT live与GPU图集之和，不含offscreen RTV/驱动元数据或共同字体buffer。旧/新按同样顺序依次预热语言页→基本页→主页；每个实际页面总量超过旧基线直接检查失败，不凭纹理单项推断内存通过。

此前关于三帧/上传前/atlas-only口径的描述已被此更新取代。仍不代表生产切换峰值、线程或nativeGUI验证。尚待构建运行。

第二次修正：页面夹具现在按生产顺序注册LegacyRegular、LegacyStrong、DesignRegular、DesignStrong及DesignIcons；Legacy两脸合并实际Segoe(36px/+10offset)与icomoon(32px/+4offset)，文字源排除e81e，FontDefault为LegacyRegular，初始ImFluent绑定与Build时机对齐产品。每帧以Legacy Caption12绘制真实本地化标题/版本并以Legacy合并字库绘制窗口控制符号，正文和导航切回Design，结束恢复Legacy。不是添加闲置分配来改变基线。不可把先前仅2Design角色的约+97KiB预算差异当作完整生产配置结论；新版本仍保留严格旧新预算比较，若失败必须处理产品而非放宽。

保留font-owner添加的INKEYS_FONT_PAGE_LOCALE和INKEYS_FONT_PAGE_SCALE环境过滤，过滤仅减少场景数量，不跳过断言；完整验收须取消过滤。

第三次补充：旧/新同一context均先通过真实ComboRow的键盘F4输入打开语言菜单，通过ImGui鼠标滚轮输入滚动到底/到顶，再F4关闭；验证实际PopupWindow活动、两端滚动、关闭及selected不变。随后预热120帧记录page0，并继续基本页→主页→返回语言页(page3)。不在测量前清理字体缓存。

在生产Control13 baked Glyphs中只读检查11种NativeLabel的全部非空白字符已存在，不使用FindGlyph补字伪造访问证据。每次backend提交后检查HasFontLoadFailure=false，最终统计仍在真实上传后。语言页首段共164帧，关闭后的120帧才验收预算；其他页面120帧。focused locale/scale过滤和完整矩阵均保持相同访问路径及断言。

第四次验证方法更新（取代固定120全矩阵）：保留所有语言/倍率/主题/页面和popup访问路径，旧/新使用相同自适应稳态规则。交互完成后至少预热24帧，且至少8个连续后端提交样本完全一致才可结束；不稳则继续至最多120帧，届时未稳失败。page0必须先真实open→scroll两端→close，44帧交互阶段不计入预热。其他页从页面切换后开始计数。

fingerprint包括tracked ImGui/FT live总量、字体与源数量、全部TexList尺寸/格式/status、实际SRV GPU字节，以及所有BakedMap条目的所属字体、字号、密度、Glyph数量和WantDestroy；同时要求body extent不变、纹理事务完成和无FT错误。读取BakedMap/Glyphs，不调用FindGlyph或通过测量加载字体。每次提交后检查失败锁存。

EN100及JP200全部主题/页面/loader仍固定120帧并输出24与120总量和完整fingerprint是否一致，以验证短预热平台；若有差异必须调查，不能据此宣称24帧普遍稳定。CSV新增actual_warm_frames、stable_samples、fixed_120、boundary24_total、plateau24_matches_final。

SaveSettingFontComparison新增可选stopAfterSubmit，其他调用默认仍3帧。自适应结束仅在已经完成真实backend提交后读回当前target一次，不额外渲染，不清缓存。最后帧仍读取完整PNG。现有raw-R8、GetTexRef及独立context测试保持。文本吞吐benchmark仍是120预热+300×3，未修改。

当前仅完成方法源码，等待串行构建/运行；没有新的测量结论。

Final full-run follow-up: two failures distinguished (2026-11-02)

- `CheckGlyphs` expected continuous stb fullwidth advance within2.5% of size. FreeType LightHinting rounds advance to raster-pixel grid; at14px optical advance13.58 vs14 is valid .42physicalpixel difference but exceeds old .35. Test now keeps the originalstb tolerance and givesFT only half a rasterpixel plus1/64 requestprecision, divided by actualsource×baked rasterdensity. It logs actual/expected/density onfailure; no shape/baseline/clipping threshold was relaxed, independentoldstb Noto negatives unchanged.
- pt-BR sponsor at260DIP overflowed right edge1px (also .5px at1.875x): wrapping usedadvance only while visibleglyphquad can exceedadvance, and centeredDrawInline rounds finalx. ProductionRenderSponsorThanks now derives maximumleft/right glyphoverhang fromthetext's actualfont andreserves it plusonephysicalpixel inwrapping budget. It stillcentersactualink withinoriginalwidth andkeeps fontsize/weight/itemwidth/colors. No clipping/ellipsis orassert relaxation; existingalllocale/themes/widthsponsor containment regressionsremain.

Files: Setting.Pages.cpp RenderSponsorThanks, setting_design_tests.cpp CheckGlyphs only. Changesreadyforfont-owner targetedrun; no newpassclaim. Entire120/24page warmmatrix unmodifiedbythisfollow-up.
