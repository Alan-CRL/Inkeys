# Draw3 兆芯上传兼容修复与诊断包 v3

## 目标与授权
用户已审阅上一轮完整实施计划，本轮明确“开启 trellis 任务并开始实现”，追加默认开启的兆芯兼容开关与 v3 诊断包，并将在兆芯与高通 GPU 实机回传。因此前一轮最终计划已经获准执行；无须再次请求实施许可。无 commit/push、无 GUI/隐藏 HWND/Computer Use。原始诊断目录只读。

## 背景
现场 KX-7000 / ZX C-1190 (Vendor 0x1D17, driver30.0.23.21)，4K，34主画布Draw帧共12727.56ms；大ink资源Map/Unmap占12686.33ms（99.68%）。独立probe大dynamic绘制四轮558.902ms，DEFAULT四轮5.956ms；大合成721.815ms，小合成2.349ms。离屏不是产品/光学测量。

## 需求
R1 全设备合成矩形使用专用256B动态结构化buffer/SRV，仅写32B，仍DISCARD；不再重置墨迹head或复用大ink资源。布局/shader/混合不变。
R2 实际硬件adapter VendorId0x1D17且兼容开关开启时，3.2MB ink/4.8MB highlighter使用DEFAULT、CPUAccess0、按有效字节范围UpdateSubresource。每批offset0，保持原拆批/连接/工具语义。其他GPU/WARP保留现有dynamic路径。覆盖stroke/shape/cursor/laser coverage/dots/highlighter。
R3 新配置Experimental.Inkeys3.Draw3.EnableHardwareAcceleration=true和EnableZhaoxinCompatibility=true。设置实验选项显示“启用绘图硬件加速”“为兆芯设备启用兼容模式”，说明重启生效；后者其他GPU不生效。启动快照随重建一致；HW关直接WARP，HW开保留失败fallback。UI RenderPipeline/DComp选项独立。
R4 v3包交付Release x64与ARM64，沿用一键采集、最终owner/停止身份、安全超时、Win7PS2用户脚本边界、只读原始包。记录请求配置与实际backend/upload模式/资源大小；DEFAULT void API不伪造HRESULT。
R5 在正常产品退出后的离屏矩阵增加同规格的优化普通(dynamic+独立小合成)与兼容(DEFAULT+同小合成)成对用例，区分冷/暖/measure/drain和CPU/GPU证据。两边相同shader/尺寸/上传量/Draw工作；不以“旧大合成”充当优化普通路径。输出可比较的表与不完整/不支持原因，不把CPU提交胜者冒充完整4K/RTS胜者。
R6 报告/README指导在同一兆芯上分别开/关兼容选项，重启并用相同工具动作重复采集；不静默改用户配置或自动伪造同一次实际书写A/B。高通保持普通路径，能与另行补充的v2对照；分析器保留v1/v2可读。

## 验收
A1(R1/2) 无窗口测试验证独立合成不改head、dynamic追加/回卷、DEFAULT连续更新/Draw与多批/工具上传，正确性有读回/可观测证据。
A2(R3) 默认/持久化/false直接WARP、兆芯开关false普通、非兆芯true不启用兼容；重建使用启动配置。
A3(R4/5/6) v1/v2/v3 fixtures、身份/版本/缺样/成对比较边界与脚本测试通过。实际有限无HWND离屏成对probe验证协议，明确是本机非兆芯性能。
A4 完整InkeysRepo.sln Debug|ARM64原生MSBuild，PATH规范化、至少5分钟；Release|x64及ARM64产物打包、manifest/hash/zip静态校验。编码换行保持，diff check。
A5 两架构v3 zip、可核查验证记录与现场A/B说明交付；最终流畅度由用户实机验收。

## 范围外
全GPU统一DEFAULT、shader/笔迹算法改写、运行中切换设备、泛化资源池/自动热基准、UI后端切换、驱动升级、GUI实测、提交推送。

## 验收记录
- A1 已通过：实际WARP生产上传/绘制读回覆盖三种上传模式、独立合成/head、连续绘制像素与多工具/拆批。
- A2 已通过：纯CPU设备策略与真实配置落盘四组合、ReadMini/copy/NoUpload/非法类型默认；启动与设备恢复链静态审查通过。
- A3 已通过：WinPS/pwsh离线测试、真实v2副本回归；最终安静环境Release ARM64矩阵56/56 complete/identity，真实分析8/8 comparable、0issues。完整产品目标机A/B由用户回传后决定策略取舍。
- A4 Debug ARM64完整solution、Release ARM64完整solution、CLI/headless与格式通过；Release x64完整solution、7项CLI及交付包检查全部通过。
- A5 ARM64/x64两份Release v3包已生成并完成所有PE/capability/manifest/ZIP entry哈希校验。任务不把本机无窗口证据写成目标机4K/RTS/Present流畅度通过。
