# 阶段三代码与无窗口验收

2026-10-12，工程与合成几何验收通过；GUI/实际设备/真实 Windows7 未执行。不 commit/push。

## 构建与回归

- 当前 VS 由 vswhere 定位，ARM64 原生 MSBuild，既有 v143、SDK、C++20、MT/NDEBUG 和 vcpkg Abseil 20250512.1#0 未变。每次同 PowerShell invocation 规范 PATH；监督超时3600秒。
- 首次沙箱构建 C1902，沙箱外相同配置解决。独立 demo 编译定位并修复 taper 签名与丢 BOM；相关7文件 UTF8 BOM/CRLF 核对，新尾段测试文件明确 UTF8 BOM。
- 完整 InkeysRepo.sln Debug|ARM64 Rebuild exit0，332.407秒；最终必要增量 exit0，15.140秒，Headless --no-window exit0。
- 既有 inkStrokeModelerTest.sln Debug|ARM64 tests 目标及 demo 依赖成功编译，修复后增量210.391秒；最终增量13.954秒及编码核对后1.172秒 exit0。
- 4个 Draw3 Shader 编译及链接完成；主产品最终 EXE SHA256=1319c099417326c8b6a426e783b913ab5085688e65b122385e79963ad6c6fa49。构建日志与报告在 Build/modeler-baseline/stage3-main-native/ARM64-Debug。
- 原 API 回放 exit0，SHA256=0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646，与阶段一/二完全一致；31项原源/头文件 hash 变化0。
- 最终 --modeler-tail-replay exit0：96窗口，150567检查，failures=[]。局部算法68Apply/12Unchanged/16Rejected（窗口不足12、匹配不可信4）；此计数与旧阶段二18Apply/12physical improvements 分开。
- --local-tail-integration-only exit0，包含容量/时限/接续清终态/Reset、修改旧 L1 范围、活跃几何不变、精确Up与属性、非笔工具及非法endpoint回退。
- 真实产品 --draw3-pen-taper-test exit0：72modelCases/36resumeCases/331480assertions/0failures。首轮原断言抓到 taper 插点后 std::copy 写回的长度/越界错误，改为保留前缀后 insert 整段，原断言未放宽。
- 求解器新增极大有限输入、窄化前范围检查；失败原子空patch。独立检查见 check.md；git diff --ignore-submodules --check exit0。

## 效果

Build/terminal-tail-preview/cpp-local-stage3 的图直接读取 C++ 报告，Python 不进行拟合。报告SHA=1f2be7953d1bb09209726a48a7596004209ae5dcb1f7b1bf4620f420f23097d1。

| 160ms场景 | 最后两段折角前/后(度) | 最大调整(输入像素) | 固定前点数 |
|---|---|---|---|
| 快转 | 89.483 / .434 | .266 | 76 |
| 圆周 | 89.277 / .175 | .394 | 83 |
| 圆弧 | 89.472 / .069 | .137 | 79 |
| 非均匀圆弧 | 88.322 / .071 | .133 | 62 |
| S弯 | 89.733 / .082 | .144 | 84 |
| 双弯 | 87.066 / .257 | .381 | 85 |
| 稀疏 | 89.211 / .144 | .130 | 63 |
| 零辅助 | 89.211 / .032 | .135 | 63 |

正常直线逐点不变；真实转角样例保留前面的转角，仅修末端pin折线。这里黑线为真实 Modeler 加离线接纳近似，红线是真实 C++ 求解结果；Draw3 Stored 合同另由实际产品probe验收。图不代表设备或GPU截图。

## 成本与边界

--modeler-tail-benchmark exit0，12312检查0失败；每case16次预热、256次采样。ARM64 Debug消费者但 Modeler/本地扩展保持专用O1/Ob1/MT/NDEBUG。普通94–96点局部求解中位68.7–153.6us，p95 74.85–167.825us；512插值压力样例中位2266–4599.15us，p95 2578.3–7302.275us。超point预算提前拒绝中位 .2–.4us。两个预测器几何相同，计时差异受系统调度影响，不解释为预测器收益。

raw固定64×24bytes样本、160ms；求解最多512参考点，工作预算有上限。成本只测求解，未测完整Stored复制、全部笔划重绘/GPU帧。保守前缀交点检查可能拒绝已有交点的笔划，零辅助不能保证恢复真实行进方向。3px形变和局部范围是当前实验起点。实时断触等待保持旧形状，确认完成时才修正；恢复视觉、实体笔、GUI、Win7及本阶段额外Release/x64/Win32未执行。

任务状态更正：用户要求保持阶段三进行中，等待人工验证；此前归档已撤回。
