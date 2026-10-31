# 第二轮直接授权：减少重复提交与像素冗余

2026-10-14 用户明确批准按前一条建议继续优化和测试。延续当前in_progress任务，不再请求规划批准。用户约束：所有UI共享WARP，Draw独立hardware，内存节制，Win7 SP1+KB2670838基础D3D11兼容。不新建device/thread/新渲染系统，不改变全局60Hz/Present策略，不commit/push/GUI。

## 本轮最小行为边界
- 当前静止设置页与完整字标hold重复生成和提交；CPU生成约1ms而WARP完成约14ms。减少精确重复的清屏/绘制/Present是主要目标。
- ImFluent无统一pending动画查询；tooltip/menu延迟、input caret和下载状态轮询依赖CPUFrame。保留CPUFrame/Continue，避免两帧相同即Idle导致功能漏更新；全事件CPU休眠涉及跨Net/invalidation接线，留作后续范围。
- 仅最终DrawData精确等于最近一次成功S_OK呈现的内容且纹理无事务/callback、无capture/restore/资源变化时跳过提交；Display/Scale、clear/backdrop/epoch、cache身份均纳入。失败、occluded、resize/fonts/主题/hide/输入/缓存事务后撤销。必须存有界值数据而非裸DrawList/ImFontBaked，不能仅用哈希判等；热路径无扩容，超预算回退实际提交。
- 已有单份页面cache在完全不透明蒙版下不回写。缓存保留以待渐退，不影响时钟/600ms/字标精度。
- 清屏候选仅在可严格证明整个目标像素会被初始化时采用；同设备基础D3D11，无依赖ClearView/保留discard backbuffer。建议Solid+style/background alpha1+整像素完整客户区域时，用现有ImGui背景列表的标题区不透明底色和缓存/蒙版覆盖客户区，省ClearRenderTargetView；所有透明/非整像素/不完整覆盖回退。原根边框、标题和混合顺序需精确RGBA验证，无法证明或无收益不保留候选。
- 同一真实时刻做完整帧/无字标/无蒙版/仅cache copy分组，定位字标、全屏混合、内存复制成本；EVENT完成计时与像素readback分开，不冒充GPU busy。

## 验收
重复静止页/hold可以少提交；真实hover、tooltip延迟、键盘输入/caret、异步状态变化、resize/theme/fonts/Hide、失败/遮挡都能立即重绘或正常推进。跳过提交不需要额外GPU纹理，不改变cache捕获和终态恢复事务。省复制/clear逐像素保持浅深、Solid/Mica/部分alpha/非整边界正确，关键失败回退有意义回归。独立基准报告重复帧提交/跳过次数、比较开销/有界存储及各组件耗时，不能把skip几千FPS当显示FPS。完整solution Debug|ARM64与全套--no-window通过；格式/i18n保持。

## 文件ownership与验证
core实现代理：Setting.cpp、Setting.Pages.h/.cpp、Setting.HomeEntryCache.h、新提交比较helper及setting_design_tests.cpp的纯CPU回归。benchmark实现代理：setting_home_warp_tests.cpp（共享helper后的真实像素/重复帧/消融与基准）。主会话只改task/spec，统一完整solution build和test。代理之间先确认helper接口，禁止互改文件或并行编译。check代理独立审查。