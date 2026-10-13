# 可见折线、自适应窗口与原始输入辅助：实施验证

日期：2026-10-13。阶段三保持 `in_progress`，人工验收未通过前不得结束任务。本轮未运行 GUI、未 commit/push。

## 修复内容与证据

现场日志原8笔全部进入旧局部求解，但3笔因候选绝对转角门、5笔因3px最大位移门未发布。新增自适应入口以可见末钩触发，以有限弧长扩窗控制调整范围；位移只记录。保留原阶段二及旧local入口、13个上游Modeler源和实时Update/Predict行为。

实际实施同时修复三项视觉问题：末端P2自由，Up只固定位置；可信原始点在内侧时提示更早入口并留过渡余量；只在有此证据时加入有限法向约束，避免前段外鼓补偿尾部内缩。稀疏模型入口向前取整，不能使请求的过渡窗口缩水。单个远处微小偏差或大离群点不应提示整窗修改。

Draw3/demo独立维护模型物理历史边界，固定128条/160ms，与raw64点及最大512模型输入窗口分离。完成态在副本中使用可变点数补丁，以有序来源插值半径/时间、映射committed边界，再走taper及Stored Stroke路径；实时realPoints不回写。临时反色只标记实际改变且发布的完成尾段；有界日志新增尝试次数、实际重构弧长、raw_hint及采样指标。

## 实际 C++ 输出图

图像程序仅绘制C++报告中的位置，未在Python重新拟合。每行新旧版本同坐标同尺度；中心线不包含实际笔宽，不能替代Draw3/GPU验收。

- 对比图：`Build/terminal-tail-preview/inward-side-6-8.png`。
- 完整8笔图：`Build/modeler-baseline/stage3-source-native/ARM64-Debug/inward-side/adaptive-preview/real-eight.png`。
- 原始预览数据：`Build/modeler-baseline/stage3-source-native/ARM64-Debug/adaptive-replay.json`。
- 最新Debug报告：`Build/modeler-baseline/stage3-source-native/ARM64-Debug/inward-side/adaptive-replay.json`。
- 最新Release报告：`Build/modeler-baseline/stage3-adaptive-release/ARM64-Release/final/adaptive-replay.json`。

| 现场样本 | 原预览重构弧长 | 当前重构弧长 | 可信raw提示范围 |
|---|---:|---:|---:|
| 6 | 42.3 px | 360.5 px | 268.6 px |
| 8 | 97.6 px | 203.7 px | 159.7 px |

上述为沿原模型轨迹的范围，不是位移。第6/8笔实际最大位置变化分别14.45/16.51px。当前8笔均Apply；第3/4笔也因形状验收扩至较长窗口（1221.2/1004.8px），不隐瞒此范围变化，需要人工确认这些更长过渡是否符合书写意图。所有窗口仍限定在调用方允许的历史范围内。

## 已完成自动验证

- ARM64原生MSBuild动态定位，v143/SDK/依赖不升级，每次在同一PowerShell调用规范化PATH；构建脚本超时3600秒。
- `InkeysRepo.sln Debug|ARM64`完整Build：退出0，产品SHA256 `0059cdd16e736595a0fe8751a82dcdc7dca33ed193f4bdf3ad294d6c22b3e87f`。
- 既有demo/test solution：Debug依赖构建与Release完整Build退出0。
- Debug和Release自适应回放：各8284项检查，0失败；所有case输出几何/来源/诊断精确相同。
- Debug和Release局部完成态集成：0失败。
- 原Modeler19场景128412检查均通过；报告SHA256仍为 `0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`，与迁移后既有基线逐字节相同。
- 产品Debug `--draw3-pen-taper-test`：334062断言，0失败，含真实模型弧线、窗口前缀/属性、断触接续等已有及新增检查。
- `InkeysHeadlessTests.exe --no-window`：退出0；未改变其MTd配置。
- 独立源码审查：`adaptive-check.md`，最终无新增确定性阻塞项；diff检查通过，已核对新增/修改C++文件BOM和CRLF。

新增反例包括：无raw、仅Up、稀疏raw、同几何加密、末向可自由旋转、入口过渡余量、远离轨迹离群点、近轨但远弧长单点、不可见末钩、真实角点、正常U/S、带pin的U形保持拱顶和弧长、合法采样/验证预算耗尽及非finite。每次失败都保留空patch。

## 性能与限制

Release尾段求解8笔各预热5次、30次测量，中位数38.3–132.8微秒；这仅为求解及其检查，不能冒充完整Up成本。Debug完整FinalizeStoredStroke（诊断I/O关闭，预热3次、30轮）无稳定前缀中位数0.1375ms、P95 0.2229ms；1000点稳定前缀中位数0.959ms、P95 0.9707ms。

Release完整产品Build及334062断言均通过。关闭诊断I/O后，完整FinalizeStoredStroke预热3次、30轮：无稳定前缀中位数0.1020ms、P95 0.1326ms；1000点稳定前缀中位数0.32705ms、P95 0.4386ms。以上是本机有界样本成本，不是任意长笔划的保证。Release产品SHA256为14343262b65228b9c5706ccba8253ec16b3c007a5e0abd1b1984acd87226e6a4。Debug/Release以只读PE资源方式确认4个SHADER资源301–304均为非空DXBC；资源检查未执行应用入口。

有限曲率探针和有限法向约束不是全连续区间数学证明；迭代不满足时拒绝并扩窗。真实急转与人工pin在缺少输入证据时仍可能难以区分。现场日志只包含尾窗，无完整前缀；完整控制器/GPU、实际笔宽及设备感受须人工验证。此轮未执行GUI、实体笔、Windows7或x86/x64新构建。
