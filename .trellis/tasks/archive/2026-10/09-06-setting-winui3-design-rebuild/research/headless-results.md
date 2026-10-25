# 首批无窗口测试结果

日期：2026-09-06。测试代码由本批测试子代理负责，完整 Solution 构建由主会话统一执行。

## 已执行

- 使用本机 VS 18 Community 的 VC/Tools/MSVC/14.51.36231/bin/Hostarm64/arm64/cl.exe，以 /std:c++20 /EHsc /MTd /W4 /utf-8 /DIMGUI_DISABLE_SSE 编译独立 CPU probe。
- probe 只调用与测试工程相同的 RunSettingDesignTests()，临时入口与产物均在 Build/SettingDesignProbe/，没有复制测试逻辑。
- 编译现有 ImGui 四个核心源文件、ImFluent、生产 Setting.Controls.cpp，不编译 Win32/DX11 backend，不创建 HWND 或 D3D device。
- 最新运行退出码为 **0**，耗时约 **3.92 秒**。对应 Build/SettingDesignProbe/compile-current.log、run-final.log。
- 测试工程 XML 解析、git diff --check -- InkeysHeadlessTests 通过；三个测试工程文件均保持 UTF-8 无 BOM、CRLF，无孤立 LF 或行末空白。
- 主会话随后执行 ARM64 host MSBuild 的完整 InkeysRepo.sln / Debug|ARM64 构建并成功。
- 主会话随后执行 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，退出码 **0**，耗时约 **8.85 秒**，输出 PASS animation correctness。当前集成 object 中已确认包含真实 Row、TextAt 多行、图标轴和字形行框的回归。

独立 probe 与完整无窗口测试集均已通过；这些验证覆盖真实 CPU 字体烘焙、控件绘制数据与公共布局合同，不代表整页 D3D 截图或人工交互验收。

## 实际覆盖

1. 导航：宽窗用户收起/展开偏好跨窄窗往返保留；覆盖展开不改变内容矩形；尺寸变化不意外覆盖用户偏好；页面左右 gutter 和边界。
2. 纯行布局：通过生产 TextHeight() 测量中文/英文混合长文，验证图标、文案、action 不相交，短/长 action 尾沿一致，窄预算重排增高。
3. 字体：实际 SC Regular/Bold、TC Regular/Bold，TC 与生产一样合并 SC 回退；12/14/28 字号在有效倍率 1、1.25、1.5、2、1.875、4 下，合计 72 组。
4. 字形：Agpqy、中文、简繁字符、全角括号和标点不使用缺字回退，可见边界落在 12/16、14/20、28/36 行框；中文全角 advance 与实际 em 一致；保留英文 descender。
5. 多行绘制：直接使用生产 TextHeight()/TextAt()，验证 72 组真实顶点的纵向行框及横向预算；没有在测试里另写换行算法。
6. 真实设置行：252/320/624 DIP × Button/Toggle/Slider/Combo × 六倍率，合计 72 组；action 回调每次提交恰好调用一次，空闲绘制不改变值，控件处于分配矩形内并靠右。
7. 末行合同：真实 SettingRow() 为 child 最后一个元素后立即 EndChild()；测试不添加额外 Dummy 掩盖光标边界问题。
8. 导航图标：从真实 NavItemEx()/DrawIcon() 顶点验证展开/收起的可见图标中心同轴；字库使用独立 Fluent Icons face。
9. DrawData：真实 Button/Toggle/Slider/Combo/Nav 生成非空绘制数据、有限 clip rect/顶点、合法索引和 CPU atlas 像素。

原生 ImGui 会对字号和控件宽度进行物理像素量化，测试允许字号至多 0.5 像素和 action 尾沿至多 1 像素的误差，仍能抓出固定列左端导致的明显偏移。

## 本轮检查发现并推动修复

- 原候选 GlyphOffset = 30 × 0.09：100% 下 Caption 12 的混合字形边界为 [1,15]，加 2 像素行框 padding 后超过 16 行高约 1 像素；Title 28 为 [2,33]，加 4 后超过 36 行高约 1 像素。实现方改为 30 × 0.025 后，上述矩阵通过。
- 校准字面用于旧 TextWrapped() 时，100% 的 14 字号混合 ink 高可达 16，超过其名义行步长。实现方因此保留旧页面原字体，新 Shell/Home/General 使用校准字体与显式行高；测试没有把新行框通过当作旧布局自动安全。
- SettingRow() 最后手动设置光标到未登记的 gap 会触发 ImGui EndChild() 断言；加入 gap 占位后，1.875 倍率仍出现 CursorPos.y = 307.5 > CursorMaxPos.y = 307。实现方统一最终物理光标取整后，所有真实末行用例通过。
- 初次测试错误地直接读取首次 PushFont() 之前的 icon bake，使用了不同的 rasterizer density，导致 UV 匹配失败。测试已改用与产品相同的 PushFont() / GetFontBaked() 路径；这不是产品图标错位。

## 测试工程改动

- InkeysHeadlessTests/setting_design_tests.cpp：上述 CPU 回归。
- InkeysHeadlessTests/InkeysHeadlessTests.vcxproj：登记回归与既有 ImGui/ImFluent/Controls 源文件、头文件和 include 路径，ARM64 禁用 SSE。
- InkeysHeadlessTests/animation_tests.cpp：调用新回归；--no-window 同时将 CRT/系统错误报告重定向至 stderr，避免第三方断言弹窗。

未修改主应用工程或生产源文件；未启动产品窗口；未提交 commit。
