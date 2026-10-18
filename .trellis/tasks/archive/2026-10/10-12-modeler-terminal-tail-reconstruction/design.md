# 技术设计

## 状态与边界

用户2026-10-12已批准实施；阶段1基线PASS。新增canonical terminal_tail_reconstructor.h/.cc作为本地扩展单列provenance，共享清单由13个未修改上游TU加1个本地TU组成，沿用阶段1编译/ABI合同。不改变StrokeModeler布局、Save/Restore或Update追加语义，不接产品。

## API与硬合同

Request包含有序真实消费raw、实际接纳reference及caller来源、固定入口/入射段、真实Up、替换区间、单位一致的预算与门槛。库不认识L1、像素、Windows、压力或文档。初版固定前两个有效参考点以保留离散入口方向，过短或退化窗口拒绝。Outcome为Apply/NoChange/Rejected与原因；成功含位置/有序source interpolation，失败无部分补丁。可复用scratch归调用方，无全局状态。

## 候选

端点驱动平滑位移为control；主候选令q=m+d，用有序raw走廊约束、弧长或非均匀间隔的位移变化正则。入口与终点是硬约束，不用巨大有限权重逼近。禁止直接压平q曲率、假设统一圆心、全局最近分支或同时间硬匹配滞后模型。无新增求解依赖。

发布前检查finite/range/endpoint/入射/source顺序、位移/弧长/采样质量和自交。不足或endpoint-safe参考无改善时NoChange/Rejected；参考尚未到真实Up时，硬门通过可单独修复endpoint，但不计作末钩改善；旧endpoint-safe几何可供未来产品回退。补丁不伪装Result，不改旧导数/压感。

## 评估与风险

真实Modeler输出按显式窗口切分，记录输入/参数/窗口与前后几何。两预测器及正负矩阵，按相同物理弧长采样比较末端转角、走廊误差、长度比、最大位移、自交、拒绝原因，不靠增加点数降低角度，不把玩具权重当生产结论。参数需有实测依据。

短窗口入口/终点可能不可兼得，raw缺失、自交对应无普遍保证。Up抽动、压力/taper接缝、Stored与Touch回滚需要后续接入/设备证据，本阶段不声称已验证。vcpkg Abseil20250512.1#0及工具链保持。
