# 字体基准夹具口径

新增 `InkeysHeadlessTests/setting_font_benchmark_tests.cpp/.h`，入口 `RunSettingFontBenchmarkTests`。由dynamic_fonts agent登记工程、CLI和运行，worker不并行构建。

- 旧stb配置硬编码为cf4d9120规则，不依赖随候选改变的生产helper。
- 候选使用生产FreeTypeTextFontConfig及atlas-level loader。
- 各场景创建独立ImGui context及真实无窗口WARP renderer，4种文字角色，每角色按主脸+三个字族fallback注册；源TTF从磁盘一次读入，在场景间共享，报告字节一次。未加载图标源，属于文字子系统夹具，不能替代实际全页面资源证明。
- ImGui allocator统计requested live/peak bytes，包含context、draw buffers、backend CPU对象、字体bake/glyph、CPU atlas；FT allocator单独直接malloc统计，避免再次经过ImGui统计。map计量器自身开销不纳入产品内存。
- CPU atlas是ImGui live的子集，不能再相加；GPU逻辑字节从全部TexList实际SRV资源desc计算，包含尚未销毁的旧图集。D3D driver元数据、堆粒度/驻留和offscreen target不算字体图集字节。
- CSV total为ImGui live + FT live + GPU atlas，不含共同source bytes；共同source bytes单列。此数字包含共同UI上下文开销，不应描述为精确全进程字体占用。
- observed_frame_peak仅是提交完成边界观察峰值；imgui_allocation_peak与ft_allocation_peak分别捕获函数内部临时分配。独立峰值不可直接相加声称同时峰值。生产切换旧新atlas同存峰值需单独运行产品生命周期测试。
- 所有11语言基本1/1.5/2倍、浅深主题；CN/TC/HK/JP/KR及en-US额外1.25/1.875/4倍。每场景真实PNG，字体ink/细节由人工对照；不是完整控件/窗口交互。
- CN/JP/KR的1与2倍深色做3个采样区块，每区块先预热120帧再采样300帧（同一context，非3次独立冷启动）。CSV median/p95/max是整个采样段的端到端CPU生成+backend+有界GPU完成等待。其余场景只4帧样本，timed=0不得拿来宣称稳定性能。
- 相同负载稳态总资源超旧基线，检查失败并输出BUDGET EXCEEDED；销毁后tracked ImGui/FT live非0也失败。尚不代表生产多语言热切换3轮验证完成。
- 使用实际生产CSO与DX11后端，无窗口/SwapChain/Present；不能等同桌面屏幕FPS或原生界面观感。

当前状态：源码待完整solution构建和实际运行。无结果时不得预报内存降低比例。
