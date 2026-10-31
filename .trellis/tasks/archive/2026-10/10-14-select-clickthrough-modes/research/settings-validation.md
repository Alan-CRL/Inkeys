# 设置与配置验证

## 实现
- UI.Bar.SelectAndClickThroughMode 为 IdtAtomic<int>，默认0、NoUpload。真实 ReadAll/ReadMini 对该字段严格接受JSON整数1，缺失及其他类型/值回退0，避免超大整数截断。
- 常规页标题后、自启前新增完整三段说明卡和现有Widgets combo。信息卡复用现有CardBackground、AccentText信息图标、TextStrong、圆角边框和留白，按同字体/同换行宽度测量高度。
- 下拉调用生产Get/Set接口，只有变化且setter返回true才QueueConfigWrite；保存沿用配置冻结副本队列。
- 三语言配置说明逐字采用已批准文案；英文主栏键保留语义单行 Select (Clear) / Click-through，由主栏局部排版转为两行，避免违反i18n换行一致合同。

## 已执行
1. Scripts/i18n.ps1 sync 和 check：en-US/zh-TW均349/349，100%，退出0。
2. owned文件 git diff --check：通过。所有9个产品/测试/生成文件保持UTF-8无BOM、CRLF，diff仅局部新增。
3. 无HWND控件回放：
   - 用bundled Python运行 research/replay-select-clickthrough-settings.py。
   - 提取实际新增Setting块、生产ComboClass和颜色栈方法，加载三语言JSONC完整文案。
   - 字体来自与198/258/257/262嵌入资源相同的SC/TC、Segoe Fluent Icons及icomoon仓库文件，复制生产合并顺序和字号/偏移；预载回放所需字形。
   - 用vswhere查现有VS安装，读取Microsoft.VCToolsVersion.default.txt定位原生Hostarm64/arm64/cl.exe，在vcvarsall.bat arm64环境编译，无需改工程：
     cl /nologo /utf-8 /EHsc /std:c++20 /DIMGUI_DISABLE_SSE /I Inkeys/additional/imgui Build/SelectClickThroughValidation/settings-replay.cpp Inkeys/additional/imgui/imgui.cpp Inkeys/additional/imgui/imgui_draw.cpp Inkeys/additional/imgui/imgui_widgets.cpp Inkeys/additional/imgui/imgui_tables.cpp /Fo:Build/SelectClickThroughValidation/ /Fe:Build/SelectClickThroughValidation/settings-replay.exe
   - 编译退出0，运行退出0。三语×0.6/1/1.5/2共12组各为：mode=0、writes=2、setters=2、clipped=0、glyphClipped=0、missing=0、overlaps=0、failures=0。
   - 双向0→1→0及重复选择验证；写盘计数桩冻结值序列为[1,0]。检查文字项边界和新绘制字形顶点在各信息/选择卡的clip内，标题与combo不重叠。
   - 初次验证夹具预载范围遗漏拼接的ASCII冒号，诊断为U+003A并补齐夹具；未修改产品字体。

## 验证边界与主会话后续
- 回放使用生产UI块和实际字体，运行时setter/Queue为计数桩；不能单独证明真实状态机、FIFO/窗口线程同步或真实窗口呈现。
- config_metadata_tests.cpp已加入真实Config读写回归：两值重启恢复、missing/非法类型与越界、ReadMini字段隔离、reset、独立冻结复制及NoUpload。等待主会话完整solution build及headless --no-window执行。
- 未启动GUI、未操控桌面、未commit/push；真实设置视觉及点击/触摸穿透仍属人工待验。

## 主会话最终结果
- 完整 Debug|ARM64 solution 编译通过；真实配置 Headless --no-window exit 0。
- 生产状态机与主栏 DWrite offscreen 均 exit 0。验证边界和人工待验项见 ../validation.md。
