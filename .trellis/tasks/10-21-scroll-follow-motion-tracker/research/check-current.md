# VisualMotionTracker 独立检查与局部修复（2026-10-21）

基于 `feature/move` / `3ace726b8d673f03d601b6882e3344ecbd103160`，按当前 check.jsonl、PRD/design/implement、完整 hook 落盘上下文和跟踪规范检查。**本纯图像阶段检查通过，可以由主会话继续产品集成；合成图与无窗口回归不构成正式 Mag worker 实屏像素、20FPS 或桌面墨迹跟随验收。**

本轮发现并修复一处实际误接受。审查覆盖生产头/实现、tracker tests、两个工程、主工程 filters 和 runner；审查者只修改 `VisualMotionTracker.cpp`、`visual_motion_tracker_tests.cpp`，按主会话确认同步 tracking spec 的候选条件/已测边界，并新增本报告。没有改 Capture 源码、公开接口、依赖、SDK、工具链、Draw3、主栏、存储或其它代理的改动；没有派发子代理、Computer Use、普通 Inkeys.exe、可见/前台/置顶或旧可见 CLI 测试、commit/push。

## Findings (fixed)

- File: `Inkeys/Inkeys/CV/VisualMotionTracker.cpp:443`、`InkeysHeadlessTests/visual_motion_tracker_tests.cpp:377`。
  - Issue: 零主模式的特殊歧义门被包在 `candidate 与主模式距离 > 2*ConsensusRadius` 内。ConsensusRadius=1.5处理px，使已经离开主一致集、但尚不到3处理px的次流完全跳过检查。独立扩展既有多数固定区域/中部运动 fixture，4原图px（约2处理px）被 accepted 为 total=(0,0)：主票.83631，runnerUp=0，314个FB-valid匹配中262个进入主一致集；52个未进入主一致集的匹配没有触发次流检查。原有10px负例会拒绝，因此原矩阵未暴露此缺口。
  - Evidence: 仅新增4px硬断言、未改生产时，完整ARM64 solution exit0，定向矩阵 exit1/105,215ms，只有这条新断言失败；6918合法运动帧继续全部接受。原始红例保留在 `Build/ScrollFollow/motion-tracker-check-majority-red.log` / `-stderr.log`。
  - Fix: 经主会话确认，零主模式（长度<.3处理px）独立检查距离>1.5处理px、空间票>.15且至少6个中部格的次流，返回AmbiguousMotion并记录次流票。常规runner-up距离>3处理px/.35门保持原值；覆盖、FB、跳变、合法位移、误差与提交门均保持原值。4/10px回归同时要求AmbiguousMotion、delta=0、total=0、referenceSequence=0。保留1/2px诊断，不把其接受近零的行为固定成未来算法必须保留的断言。新增局部中文注释，公开API未变。

同一不可变canvas、同一83.3%固定面积/中部1/6运动区域的实际边界如下。残差为主平移裁剪后处理px，中位数均约.0002–.0003，不能用该低残差掩盖被排除的次流。

| 中部运动（原图px） | 修复前 | 修复后 | 主空间票 | 次流票（前→后） | 主残差P95（处理px） |
| ---: | --- | --- | ---: | --- | ---: |
| 1 | 接受近零 | 接受近零 | 1.00000 | 0→0 | .61849 |
| 2 | 接受近零 | 接受近零 | 1.00000 | 0→0 | 1.00003 |
| 4 | 接受零，错误 | AmbiguousMotion | .83631 | 0→.17014 | .00214 |
| 10 | AmbiguousMotion | AmbiguousMotion | .82113 | .17887→.17887 | .00197 |

## Findings (not fixed)

- **当前混合内容合并边界（设计决定）**：1/2原图px中部运动仍在现有1.5处理px一致半径内，主一致集包含全部匹配，输出约(0,-.00009)。这是本次实测保留的模型合并规则；没有将其称为物理上不可观察，也没有承诺任意混合pane或少数区域微小运动的目标选择。主会话明确要求本阶段不扩展新的更细多模态算法；已同步 `.trellis/spec/native-desktop/scroll-follow-tracking.md` 的条件和限制。
- **实屏/产品验收（超出本阶段与本轮可见测试权限）**：正式MagnifierThread真实变化像素、角色排除、持续帧率/长期资源、普通Freeze前后、Draw3双窗穿透与真实墨迹跟随仍须独立验收。旧owner的PrintWindow诊断和本次离线合成图不能替代这些证据。没有运行其他架构、Win7/8.1、实际浏览器/Word/PDF/视频或笔/触摸。

除上述明确保留的设计/验收边界，没有发现本阶段范围内尚待修复的已确认源码问题。

## 审查核对

- 图像布局先于Mat构造校验宽高/格式/stride/size与乘加溢出；Gray/BGRA、最小末行size、padding、alpha0、奇数尺寸与借用缓冲立即销毁均有真实像素测试。resize产生自有参考；按各轴真实半尺寸比例还原一次。
- 只有可信帧提交reference/tracks/total。拒绝只增加拒绝诊断/暂停状态，保留可信参考和终身出生锚点；短拒后的恢复覆盖全部跳过位移。第三次普通拒绝sticky Paused，严重无效立即Paused，Process不自动重建基准。
- 细化后重新反向LK、FB/finite/patch检查、参考格归一化票、全FB-valid位移场变形诊断均位于实际路径；缩放/旋转模型只用于拒绝，先于平移内点裁剪。固定UI多数角点、动态干扰与空间歧义有正负例。
- 原点数预算跨格时也裁剪，旧锚点优先保留、可信补点下一帧参与，最多320。没有GT进入生产、deadband、取整、累计重置、OpenCV全局thread/RNG策略修改或新线程/窗口/平台依赖。
- 两工程/filters/runner登记一致；严格专参出现在任何位置时，有多余参数均在其它测试和窗口路径前exit2。

## Verification

- **Lint: pass**。本层没有可复用的独立C++ lint/clang-tidy目标。`git diff --check --ignore-submodules=all` exit0；新增源另查尾随空白与末尾换行。7份生产/测试/工程文件strict UTF-8、原BOM、物理CRLF通过；3份工程XML使用XmlDocument.Load解析通过。tracking spec保持接手时UTF-8/EOL。静态/hash记录为 `motion-tracker-check-static-final.json`；diff日志为 `motion-tracker-check-diff.log`。检查范围为主树，没有递归修改或宣称验证第三方子模块。
- **TypeCheck: pass**。复用现有 `Build/ScrollFollow/build-motion-tracker.ps1`，vswhere定位当前VS安装的ARM64原生MSBuild，require_escalated正常宿主；同一PowerShell保存并规范单一Path、设置MSBUILDDISABLENODEREUSE=1、TEMP/TMP=Build/ScrollFollow/TestTemp。完整入口为 `InkeysRepo.sln /nologo /nr:false /m:1 /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64 /v:minimal`。每次进程等待上限600,000ms。最终exit0，MSBuild18.99s/外部19,470ms，0 warning/0 error；没有为环境改源码/工程或删除PDB。
- **Tests: pass**。最终定向exit0/90,771ms/stderr0B；最终完整`--no-window` exit0/153,486ms/stderr0B，包含`PASS visual motion tracker failures=0`和末尾`PASS animation correctness`。同一最终产物的三种额外/位置参数组合均exit2、stdout/stderr0B。

| 实际命令/阶段 | Exit | 外部耗时 | 原始日志（均在Build/ScrollFollow） |
| --- | ---: | ---: | --- |
| `build-motion-tracker.ps1 -Round check-baseline` | 0 | 3,677ms | `motion-tracker-build-check-baseline.log` / `-console.log` / `-stderr.log` |
| `build-motion-tracker.ps1 -Round check-majority-red` | 0 | 8,884ms | `motion-tracker-build-check-majority-red.log` / `-console.log` / `-stderr.log` |
| 新4px回归、修复前 `--visual-motion-tracker-only` | 1（预期红例） | 105,215ms | `motion-tracker-check-majority-red.log` / `-stderr.log` |
| `build-motion-tracker.ps1 -Round check-final` | 0 | 19,470ms | `motion-tracker-build-check-final.log` / `-console.log` / `-stderr.log` |
| 最终 `--visual-motion-tracker-only` | 0 | 90,771ms | `motion-tracker-check-only.log` / `-stderr.log` |
| 最终 `--no-window` | 0 | 153,486ms | `motion-tracker-check-headless.log` / `-stderr.log` |
| 三种多余/位置argv组合 | 每次2 | 每次<10s等待限 | `motion-tracker-check-bad-argv.json`及对应1–3日志 |

最终14组6918个合法运动帧全部接受、rejected/paused=0；每个prefix仍严格要求`max(3原图px,path*.002)`，两个静止300各要求.5px。静止/微噪声最大0/.00613px，12屏亚像素.91984px，2048微步.12196px，水平10屏.35779px；全初点离屏/多轮接力和点数<=320断言通过。与实现者冻结矩阵逐项比较，误差/轴误差/step/FB/覆盖/接力/点数等数值诊断全部一致；最终定向与完整无窗口也一致，仅调用耗时不同。比较数据在 `motion-tracker-check-sequence-comparison.json`，命令/耗时/hash汇总在 `motion-tracker-check-verification.json`。未以删帧、放宽误差、GT选择结果或调OpenCV全局策略使测试通过。

最终源码SHA-256（构建后再次核对7个源/工程文件均未变化）：

| 文件 | SHA-256 |
| --- | --- |
| `Inkeys/Inkeys/CV/VisualMotionTracker.h` | `813509CEC36A4EF0B1124CF9B029DDB08532A5D76D3A2750BC3F121A284363A2` |
| `Inkeys/Inkeys/CV/VisualMotionTracker.cpp` | `E336887415B4A0128AF4C742B1110587DED1DD2E4E83EC43991DB48BD7FFF4C0` |
| `InkeysHeadlessTests/visual_motion_tracker_tests.cpp` | `FF9C0B7FB0A8D7F5AA94EF7F8855937D9A08C31C2EB537DCD70517E8DBD9EA6A` |

最终产物SHA-256：

- `Build/ARM64/Debug/Inkeys.exe`：`0A644C49A0221DAF441013665DE90F70D22B4D566008EFF3344BE79A623A4827`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：`D0ACC189BE7DBABFC3CED86052C75E4704CE8AC56DB926B6A52205B68C4D9FF4`。

哈希只标识本次构建和验证对象。固定960×540/961×541的Debug离线Process耗时不代表真实主屏Capture+Tracker帧率，也不证明产品GUI或旧系统运行。必要实屏验收仍是父任务的停止门槛。本轮未遇自动审批拒绝。
