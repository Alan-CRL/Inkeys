# 技术设计（第四版已批准）

## Boundaries and data flow

沿用单任务完成设置/配置/启动/绘制的同一条闭环，无独立可发布子任务。`Config schema -> ReadMini -> StartOptions frozen appearance/effect -> StartupPreview Runtime -> shared D2D -> GDI/ULW`；设置采用已有 draft/events/QueueConfigWrite 与条件块，不额外引入偏好存储。

建议枚举 `Shimmer=0, Aurora=1, PixelAssembly=2`，持久化为 `Experimental.Inkeys3.UI3.StartupPreview.Effect` 整数，缺失/非法回退 0。配置 normalize 与 runtime resolver 使用同一有限域。StartOptions 增加外观与效果信息，Runtime 只读取副本；不逐帧读取全局 Config。复用 Bar.ThemeMaterial 的角色/透明度解析而不导入完整 Bar renderer。

## Visual contracts

### 掠光

保留旧斜向扫光的周期、方向和离屏停驻；为浅色基底提供可见而克制的中性反光版本。无额外最短播放门。

### 彩虹晕染

固定底板与真实主栏等大，保持圆角矩形、不变形、不摆动；角落采用现有超椭圆。基底按有效材质选择雾白或深色，RGB/alpha 和边框保持稳定。内部使用固定随机 seed 初始化局部不规则色块，位置和颜色没有排序；提高色块密度并以连续低频彩色底场补足空隙，避免大面积仅剩原底板。色块二维移动、旋转/伸缩、柔和排斥碰撞，自由交融。尺寸接近数个主栏高度而非一整段竖向色带；离屏 Screen/受限亮度合成并按需 GaussianBlur，再叠加同一掠光，使用实际形状 mask 裁剪。使用明亮协调调色板、限制总色层不透明度，避免乘法混色或互补色连续相乘产生脏灰/褐色。色层在内部连续有色且深浅两种主题都使用较低不透明度（v4 样式稿均约 0.20），透出原底板；删除全部额外内侧羽化/边框过渡，仅保留实际形状 mask 的正常抗锯齿裁剪。色彩、反光、基底属于同一背景组；进度在组外最后绘制。深浅均保持轻淡染色和反光，不能只降低深色；样式稿反光强度为原掠光的约 0.45，避免整个底板持续发白或混脏。周期收尾连续。

### 像素拼装

从与掠光完全相同的基底+边框离屏 RGBA 按块采样 target；仅 alpha>0 的像素参与，保留边缘原始颜色/覆盖率。全透明块跳过，块内透明像素保持透明，不能把部分透明边缘改成不透明方块。只在初始化/几何失效时构建，不每帧 GPU readback。像素 origin 随机分布在目标内部，outward 随机点在完整 `sourceBounds` 内，包含目标形状内部，删除旧版强制排除目标区域的逻辑；origin 与 target 不固定一一重合，入场先是乱的。中央外散矩形 `2W × 3H`，底边中点对齐目标；角落按目标约 0.6W/0.7H 外扩并裁入有效工作区，保留屏幕内侧空间。散布点生成后，目标区域内的点分配附近但明显不同的随机目标（样式稿预期偏移20~44DIP，最低14DIP），再通过空间网格选择该邻域内未占用目标；其余点分配剩余目标，保持一一对应与完整 RGBA 覆盖。不能使用最近像素匹配而让内部点近乎不动。回收使用轻微弯曲的二次Bezier路径，控制点限制在允许散布区域内；这样内部点仍有可见流动，且不改变全域随机分布。所有粒子约 0.8s 同时开始回收并 2.4s 同时归位，共用归一进度，不能按路程分别完成。粒子数量从逻辑尺寸与点距产生，设置有界预算；不能随 DPI 平方增加逻辑点数。

借鉴 HiStart 的点集插值与历史帧扩散，使用 steady_clock 分两段运动：约 0~0.8s 从 origin 渐显外散到 outward；约 0.8~2.4s 从 outward 沿连续弧线回到精确 target，共用 smoothstep 归一时钟，内部点也持续流动。渐显约 0.38s 与第一段并行，外散可小幅错峰，但所有粒子的回收阶段共用起止时刻，两段速度连续收束；不能先在外围凭空显现。t>=2.4s 后位置恒为 target，取消所有微浮；图像应与完整掠光底板一致，不保留点阵缝隙。随后只让残余青色拖尾约 1.1s 消退，底板立即接入原掠光；若程序已完成则直接按既有顺序整体淡出，不额外等待青尾消退。

透明 BGRA 离屏图只含粒子和拖尾，不填充整个扩展矩形。拖尾使用双缓存保存历史覆盖率：上一帧按实际 dt 衰减、GaussianBlur 扩散、ColorMatrix 染成主题青色，仅在外散/回收运动段注入当前点迹；拼装结束后禁止再次注入，已有历史只扩散衰减至清零。清晰粒子在上方绘制，完成后可切换同一完整底板缓存，切换必须像素等价，不能靠突然盖一块底板掩盖未拼完。使用已存在的 shared D2D 设备/epoch，避免帧率变化影响颜色、模糊长度或聚合时间。效果输入不能同时为绘制 target；切换前 SetTarget/SetInput 明确断开。

## Opacity and coordinate contracts

- 有效 Light 仅为 ThemeMode=Light 且居中主栏；角落为 Dark，但仍消费用户选择的三效果之一。
- 背景用户百分比在最终背景组应用一次；内容百分比仅作用于轨道和进度，视觉淡入再单乘；ULW SourceConstantAlpha 只用于整体交接。
- 背景基准展开 0.8 / 浅色主题默认 0.92，角落 0.6，显式用户值按正式主按钮 0.6/0.8 比例。分组内选取归一基底/材质 alpha，避免最后画反光而累加增实。
- 把正式 `targetBounds` 与粒子 `presentationBounds` 分离；目标相对扩展 HWND 平移 offset 后绘制。扩展区域与模糊外扩裁入有效屏幕，真实栏位置、缓存宽度和交接诊断保持原目标语义。
- 像素块尺寸、外散位移、模糊 sigma 以 DIP 定义，统一映射 UIBase×Zoom；DPI/target像素坐标只换算一次。背景效果 clip 与进度布局消费同一个目标矩形。
- 保留 per-pixel alpha 的空白区域；扩大 HWND 不可通过整块不透明底将空白区域变为鼠标阻挡区。实施时复核已有 WM_NCHITTEST/ULW 逐像素命中契约，不能为便利改变正式 UI 输入。

## Completion and failure

以 preview 首次 committed 显示时刻为视觉 epoch。成功准备增加纯策略条件 `effect != PixelAssembly || elapsed >= assemblyDuration`，仅延迟进入已有 handoff，不能阻塞或伪造 milestone。已加载但未拼完保持预览；未加载则进入静止底板的掠光等待。3s 进度门保留；正常完成在 3s 前不突然显示满条。失败立即采用主题对应红色轨道，终止最短播放等待；退出/资源降级沿现有有界清理，不能使正式 Bar 永久留在 alpha 0。效果重建保留 epoch/seed/进度，不从头播拼装。

## D2D feasibility and risks

用户已允许 D2D effects。项目 `Bar.Rendering.cpp:1766` 和 `:2371` 已有 GaussianBlur + SOFT 边缘 + 3 sigma padding 的生产模式，可按需复用策略但不搬整个 renderer。GaussianBlur/ColorMatrix 为 D2D 1.1 基础效果，微软文档注明 Windows 8 与 Windows 7 Platform Update 支持；仍不等于本机已完成真实 Win7 验证。

- [Gaussian blur](https://learn.microsoft.com/en-us/windows/win32/direct2d/gaussian-blur)：alpha 同样模糊，SOFT 输出会外扩，sigma 为 DIP；不得漏算 output offset/padding。
- [Color matrix](https://learn.microsoft.com/en-us/windows/win32/direct2d/color-matrix)：默认 premultiplied 模式先反预乘再处理后预乘；保留输入 alpha，避免通过常量 alpha 产生不透明矩形。
- [Composite](https://learn.microsoft.com/en-us/windows/win32/direct2d/composite)：按正确输入次序合成透明层。

风险在高 DPI 扩展窗口的 WARP 带宽和历史缓存；使用受限点数、局部低分辨率模糊缓存及可靠的尺寸/epoch 失效。实施中验证实际开销和 effect 创建失败回退；不得未测宣称跨机性能或 Win7 GUI 已通过。两款第四版样式已批准，原生离屏渲染完成后对照该样式；效果细节可因原生抗锯齿略有差别。

## Compatibility / rollback

默认 0 保持旧用户选项；新增效果与新增设置行构成最小范围变更。回滚时配置中未知 Effect 沿 normalization 回退旧效果；旧版本安全忽略新字段。保留既有 Enable/宽度缓存与二十语键生成方式。源文件编码、BOM、CRLF 按原样保存，不清理既有工作区改动。
