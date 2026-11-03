# 首轮证据与方案候选（规划中）

基线：feature/i18n @ cf4d9120，2026-11-02。仅阅读代码和固定版本上游资料，未变更产品、依赖或运行 GUI。

## 1. 当前已经使用动态字形，不能按 Inkeys2 的历史机制直接推导

- `Inkeys/additional/imgui/imgui.h:32-35`：固定 1.92.7 / 19270，支持新纹理 API。
- `Inkeys/additional/imgui/imgui_impl_dx11.cpp:231-235,432-520,729`：已启用 RendererHasTextures，并处理图集创建、增量更新及延后销毁。后端还有项目自己的 HRESULT/故障注入/资源恢复处理，不能用上游文件覆盖。
- `Inkeys/additional/imfluent/imfluent.cpp:2429-2435`：语义文字调用 `ImGui::PushFont(font, size)`，是 1.92 动态字号路径。注册时的 30px 不代表所有文字都是30px位图缩小。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1855-1950`：4 个文字角色（legacy/design × regular/bold）和独立图标；每个文字角色都注册 SC/TC/JP/KR 补缺源。语言主要决定源优先级，不是只有当前语言字体被注册。FontDataOwnedByAtlas=false 引用程序资源。
- `Setting.cpp:1880,1899,1915,1928`：FontScaleDpi 与各字体源 RasterizerDensity 同时使用 settingGlobalScale。
- `imgui.cpp` 的 UpdateCurrentFontSize 将最终字号按 style DPI 缩放并设置 CurrentRasterizerDensity；`imgui_draw.cpp:4725-4727` 实际栅格密度仍乘 src->RasterizerDensity。这说明高 DPI 有额外烘焙密度成本；不是简单地认为配置注释写 LEGACY 就已无效。对清晰度的因果程度需要像素和资源实测。
- `Setting.cpp:4979-4992`：已有帧边界字体重建分支，但同时 Invalidate/CreateDeviceObjects；可复用生命周期仲裁，评估只提交字体纹理事务而非重建全部DX11固定对象。禁止未验证就删掉失败恢复链。

## 2. FreeType 1.92.7 的真实合同

固定版本来源（不使用 master 代替当前版本）：
- [官方 FreeType README](https://github.com/ocornut/imgui/blob/v1.92.7/misc/freetype/README.md)
- [官方 loader 源码](https://github.com/ocornut/imgui/blob/v1.92.7/misc/freetype/imgui_freetype.cpp)
- [官方 loader flags](https://github.com/ocornut/imgui/blob/v1.92.7/misc/freetype/imgui_freetype.h)
- [官方字体指南](https://github.com/ocornut/imgui/blob/v1.92.7/docs/FONTS.md)

核验结果：
- 支持 ImFontLoader 和动态图集，字体源内存须持续有效直到移除/销毁。
- OversampleH/V 被忽略，不能用提高 Oversample 作为 FreeType 清晰度方案。
- 默认 hinting 与 LightHinting 是候选；会影响字形轮廓及 advance，LightHinting 倾向保留横向间距；不能预先保证视觉字重不变。
- 源码 FontBakedInit 使用 FT_SIZE_REQUEST_TYPE_REAL_DIM（非 bitmap 模式），不是可以直接假定的 nominal/em 大小；仍使用 ExtraSizeScale，Ascent/Descent 带像素取整。上轮 stb 比例可作对照起点，不应直接删除全部补偿或认为可原样无验证迁移。
- 禁止用人工 Bold/Oblique、重复描边、换字体来伪装清晰度提升；保持真实 regular/bold 资源。
- 官方说明 FreeType 覆盖率按线性空间混合解释，完整 sRGB/线性转换会影响 ImGui 默认样式。当前产品 shader 直接 `vertexColor * sampledTexture`；因此全局更换 gamma/混合不能作为无影响的小修改。先比较现有混合下的 hinting/密度，若必须调整，需隔离文字覆盖率并证明背景、图标、图片、半透明及缓存重放未改变。
- 标准 loader 输出灰度抗锯齿（FT_RENDER_MODE_NORMAL），不是 ClearType/LCD 子像素方案。

## 3. DX11 / 图集内存候选

- `imgui_impl_dx11.cpp:439-480` 目前要求 RGBA32，并创建 R8G8B8A8_UNORM 单层纹理。CPU与GPU图集分别计算；WARP 使用系统内存，不能把“GPU资源字节”叫独立显存。
- `imgui_impl_dx11.cpp:638-653` 默认线性采样且有nearest sampler。全局切nearest会影响烘焙线条/图片等，不是优先方案。
- ImGui 1.92.7 已有 CompactCache 和未使用 bake 回收（imgui_draw.cpp:2678,3935,3950,4243）。需验证触发时机、长期开窗和隐藏时的回收，不能笼统声称动态字形会自动限制峰值。
- 优先消除冗余栅格密度、仅烘焙实际显示字形/字号、保留有界预热和资源复用。不要预烘焙所有语言全部字形或无限常驻语言×字号组合。
- R8图集/SRV通道映射可以作为实测后候选，涉及后端纹理格式、白像素/AA线和彩色图形边界；不预设本轮必改。先测现有RGBA32优化后能否达到预算。
- 不使用每帧CompactCache、每帧重建、额外线程/设备，或更改全局UI调度来掩盖字体成本。

## 4. 外观与现有测试基线

- `Setting.Typography.h`：Caption 12/16、Body14/20、Title28/36、Control13/20；Harmony/Noto各有stb度量补偿、主脸基线与fallback源缩放。
- 上轮 Noto 已由用户确认生效；保留该视觉基线（任务 archive/2026-11/11-01-i18n-eight-locales）。
- `InkeysHeadlessTests/setting_design_tests.cpp:5924-5940,6128-6142` 的真实字体夹具复制了当前缩放/密度路径，迁移需使用同一生产配置，并保留独立基线，不能只把测试预期同步成新实现。
- `setting_home_warp_tests` 已有真实 DX11 WARP 离屏捕获，可提供浅深、多语言、多倍率前后对照，不需要打开GUI。
- 内存分为字体源映射/FreeType face与size/bake与glyph/CPU atlas/GPU纹理/瞬时旧新图集；性能分为冷首帧、首次遇新字、语言切换、字号/DPI变化及热帧 median/p95。
- 视觉验收需明确：保持字体与字重文件、语义尺寸/行框/控件几何；抗锯齿像素必然变化，不能要求与旧模糊像素完全一致。需要对实际墨迹bounds、基线、advance、换行、裁切、细节和整体粗细共同审查。

## 5. 语言即时切换不是单次 load

- `Setting.cpp:2911-2922` 当前先持久化选择、load，再提示重启；load返回值未用于字体/语言事务提交。
- `IdtI18n.cpp:87-124` 先构造候选map，成功后锁内替换map与identifying；文本读取有 shared_mutex。需复用这一失败保留能力，确保配置/字体/可见文案不提前分离。
- `Bar.Button.cpp:267起` 保存按钮标签和组件注册元数据。`Bar.Format.cppm:12-24,56` 的缓存key含font collection/family/locale等，但已有持久对象仍需收到语言变更。
- `PageControl.cpp:941-978` 含状态、分页按钮、白板菜单文案，需确定这些表面是否属于本轮热切换范围。
- 设置已有font serial、session epoch、主页入场缓存和FrameSubmissionCache。语言/字体刷新必须纳入这些失效键，禁止复用旧ImFont/ImFontBaked/UV/DrawData。
- 推荐在共享渲染线程安全帧边界发布语言版本，替换设置字族与文案、刷新主栏及测量缓存；不销毁整个应用/设置窗口，不中断绘图、PPT、白板或用户输入状态。具体发布与失败回退需继续设计。

## 6. 依赖现状

- 根vcpkg.json已有固定 builtin-baseline，当前无freetype直接依赖。
- 本地Vcpkg端口目录的freetype是2.14.3，但这不证明项目固定baseline解析版本就是2.14.3，实施前需锁定实际解析结果。
- 优先现有vcpkg manifest和triplets，针对嵌入TTF评估 default-features=false 避免无用压缩/图片依赖。不新建构建系统、不升级整体baseline。
- HeadlessTests 的VcpkgEnabled=false但已引用安装目录，需同步产品与测试依赖和配置，不因测试单独build缺依赖而污染工具链。
- 后续实际主程序编译按根AGENTS：完整 InkeysRepo.sln Debug|ARM64、ARM64原生MSBuild、同进程Path规范化，至少5分钟。

## 7. 待用户产品决策

首要：无重启语言切换是否覆盖全部程序自有界面（含主栏展开/组件、白板/PPT分页、tooltip、新打开的消息框），或本轮仅设置+主栏基础按钮。已有对话框是否保留打开时文案、之后新开的采用新语言，需要统一验收边界。

后续：根据范围收敛视觉兼容容差与可接受冷切换/内存预算。不要凭源码替用户作产品取舍，也不要要求用户回答可通过实验测量的技术事实。

## 8. 首轮范围确认后的补充（2026-11-02）

用户确认第7节范围问题：全部程序自有界面热切换；已打开的独立消息框保持原语言，新打开的采用新语言。第7节的范围问题已解决，不再作为阻塞项。

新增证据：
- Bar.Rendering.cpp:291-292 在初始化时将 locale.fontFamily 缓存在成员中。更新按钮文字但不更新该成员，日韩热切换仍会使用旧字族；渲染、测量、适配字号和DWrite格式缓存需同版本更新。
- MessageBox.Window.cpp:554-557、787-900 按会话持有私有字体集合和title/body FontFamily；1597等恢复路径会清空字体对象。不能仅凭“已创建FontFamily”就声称旧消息框不会变语言：需追踪创建时语言快照，以及DPI/主题/设备恢复重建时仍从同一快照选字族。
- 既有上一任务验证文档未提供字体运行时内存基线数据；不得编造MB节省或FreeType性能增益。后续应先建立同机同负载基线。
- 根VcpkgTriplets使用 arm64/x64/x86-windows-static-v143，沿既有静态链接边界引入依赖；不增加运行时字体下载或FreeType DLL部署流程。

资源验收提案（待用户确认）：
1. 同语言、同页面访问路径、同窗口面积/主题/DPI/用户倍率，在固定预热后，字体相关稳态运行时资源总量不高于当前基线；分类记录字体源映射、loader/face/bake、CPU图集和D3D纹理，避免总进程噪声和重复记账。
2. 切换期间允许短时新旧资源共存，但须有明确生命周期和峰值记录；连续循环11语言及DPI往返后回到相同场景，资源回到稳定区间，不随次数增长。
3. 默认先按实际显示像素烘焙，比较hinting，不通过全语言预烘焙、长期保存每语言整套图集或全局超采样换质量。
4. 报告冷/热帧CPU median/p95、字形初次生成和切换峰值；性能异常触发调查，不用不可靠的绝对毫秒阈值掩盖同机回归，也不以清晰度达标为由豁免明显卡顿。

## 9. 规划收敛结论（2026-11-02）

第7/8节的内存提案已获用户确认：同场景稳态字体相关内存不增加，允许有界、可回收的切换峰值。此前“待确认”描述保留为研究时间线，最新需求以prd.md已确认决策为准。

进一步核查：MessageBox.Window.cpp:788-900确实从request->language加载及重建字族；MessageBox.cpp:235-236也复制language并按同一language解析按钮。优先复用并验证现有请求快照，不为已满足的行为重构消息框。

Bar.Rendering.cpp:288-300已存在ConfigureLocalizedTypography；热切换应调用/扩展此入口及各renderer实例版本刷新，不另造平行字体配置。

prd.md、design.md、implement.md及两个非空spec/research manifests已准备。规划保持单个集成任务、四个实施阶段加基线阶段，理由详见design.md；暂无阻塞规划的产品决策。FreeType具体hinting/补偿与资源实测为实施实验，不能以此放松外观、稳态预算或性能要求。尚待用户对最终规划摘要明确批准后才start。
