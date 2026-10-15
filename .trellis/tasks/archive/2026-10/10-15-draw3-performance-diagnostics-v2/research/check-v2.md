# 诊断包 2 独立检查

检查角色：trellis-check。日期：2026-10-15。读取完整保存的 hook、check.jsonl 所列规范与研究、prd/design/implement、recorder-interface.md、probe-interface.md；未派生其他 agent，未提交或推送，未启动产品 GUI。

## Findings (fixed)

- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.cpp`
  - Issue: `gpu_ready_draw_samples` 原先同时累计 frame/prior 的 ready Draw 行，可能扩大实际帧 GPU 覆盖。
  - Fix: 分开计算 `gpu_ready_draw_samples` 与 `gpu_ready_prior_draw_samples`；要求域与 frame=0/非0身份一致，保留 Draw、ready、频率及完整 marker gates。纯 CPU fake-query 测试证明 5 个 prior 样本不能进入 1 个 frame 样本的分母，缺 marker/失败 query 不能进入覆盖。
- File: `Draw3.Performance.h/.cpp`；根会话同步 `Inkeys/Launch/IdtPerformanceLaunch.cpp`
  - Issue: Host 的详细覆盖标志考虑 site 碰撞，进程完成握手原先未携带此事实，完整性口径不一致。
  - Fix: 累计 `graphicsSiteCollisions` 到 ExportStatus；完成 JSON 与 session 均要求 full/group/resource/site 完整。session 同时要求其他原始文件导出成功。独立 slow 前缀饱和、full 仍完整不再被误作详细覆盖缺失。CPU probe 覆盖慢前缀饱和与人工站点碰撞；根会话补 14 类完成握手检查。
- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.ResourceProbe.cpp`
  - Issue: EVENT `GetData` 仅凭 `HRESULT=S_OK` 即标 `drain_complete`，未核返回 BOOL。
  - Fix: 将真实有界等待放入私有可注入接缝；仅 S_OK 且 ready=TRUE 完成。S_OK/FALSE 与 S_FALSE 继续到原截止，失败即时停止。保留最后真实 HRESULT 与 poll 数；CPU 测试覆盖最终就绪、期限未完成、API 失败和已过期限零轮询。
- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.ResourceProbe.cpp`
  - Issue: NO_OVERWRITE 墨迹的 shader 索引偏移误用于辅助 48B CB 的 raw offset，实际常量始终写到缓冲首地址。
  - Fix: 常量 raw offset 记 0，structured data 保留原实际偏移；未改变任何 D3D 参数或写入语句。真实 40-case 数据验证常量 offset=0，以及两 backend 的 NO_OVERWRITE 生命周期范围均不重叠。
- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.Performance.cpp`
  - Issue: 新探针变量 `small` 被 Windows SDK `shared/rpcndr.h:201` 的 `#define small char` 替换，导致首个 C2628 及后续解析错误。
  - Fix: 仅改探针变量和 8 处引用为 `probeCapacities`。未改 SDK、未 undef 系统宏、未改变项目工具链。
- File: `Inkeys/Inkeys/Drawing/Draw3/Draw3.ResourceProbe.cpp`
  - Issue: MSVC 上 DWORD 直接赋 Json::Value 存在有符号/无符号重载歧义。
  - Fix: `final_pid` 显式构造 `Json::UInt(GetCurrentProcessId())`。顺扫其余新增 JSON 数值，DXGI ID 为 UINT、内存为 U64、结果有显式 cast。
- File: `Scripts/Draw3Performance/Analyze-Draw3-Performance.ps1`、`Tests/Test-V2-Fixtures.ps1`（通知 package owner 后由其修复）
  - Issue: 初稿未充分验证 graphics group 的复合 key/枚举/分母、资源角色/站点、retained 与声明容量、逐帧十类操作摘要之和及 masks。损坏 raw 可能重复汇总或生成错误残差。
  - Fix: owner 已补严格校验与负夹具；非法摘要保留真实工作帧，但 leaf/residual 不可用并标 incomplete。site 碰撞与慢前缀完整性同步 native 口径。
- File: `Scripts/Draw3Performance/README.md`、中文报告（package owner 修复）
  - Issue: 同为 48B CB 的不同写法未清楚披露实际写字节差异。
  - Fix: 明确 production_field_stores 为 36B、memcpy/volatile 为 48B，跨写法不能当作完全等写入量对照。

## Findings (not fixed)

- 目前没有需再修改生产绘制逻辑的已确认问题。绑定数组明细只记录可用的首/相关资源及 slot/count；部分历史外部 view 仍是显式 unknown，这是已交接的元数据范围，不能据此声称重建全部 D3D 绑定状态。本次未扩大 Renderer 公共接口或在热路径追加 COM 查询。
- 没有剩余需修改的已确认问题。脚本 resources/gpu 逐行验证、矩阵数值表、最后 Draw 分母保护及单位修正均已核对并通过对应回归。
- 目标兆芯、真实 Win7/PS2、正式产品 GUI/RTS 与光学呈现未在本机验证。现有纯 CPU、离屏 D3D 和编译结果不替代这些现场边界。

## Verification

- Lint: pass。`git diff --check` 退出 0；44 个 baseline 文件 BOM、bare-LF、EOF 与原工作树一致，三处自修原生文件维持原 BOM 情况及 CRLF。
- TypeCheck: pass。根会话完整 `InkeysRepo.sln Debug|ARM64` 最终第 3 轮自然退出 0；证据 `Build/Draw3PerformanceV2Validation/build-debug-arm64-final3.json`。使用动态定位的原生 ARM64 MSBuild、同 invocation PATH 规范化与既有 Solution 配置；未另起并行构建。
- Tests: pass。最终 Debug ARM64 8 个早期 CLI 的自然退出与预期全部一致；性能 recorder/production proof、14 类 launch、config、resource CPU/list 及 3 类非法参数均通过。Headless `--no-window` 自然退出 0。证据 `cli-debug-arm64-final3/results.json` 及根会话 headless 日志。
- Native budget: `payload=32,627,128` B，低于 `33,554,432` B；Frame=800B、Call=128B、Group=112B、Resource=96B。包含 Impl 和全部固定数组；无正常路径新增 clock/metadata builder/分配/query，CPU off probe 通过。
- Production wiring: 7 个 v2 原始快照 TU、309 个原 D3D/context/device 调用的顺序及参数独立对照一致；ULW 两个互斥 helper 分支各只执行一次原 Unmap，GetLastError 在结束 clock 前锁存。结果 `review-native-static.json`。
- Real offscreen protocol: 根会话 Debug ARM64 矩阵 40/40 complete 且身份验证通过，约 10.627s；仅独立离屏 D3D，无 HWND/swapchain/Host/config。独立只读复核 6,296 calls、170 resources、240 GPU 行（204 ready、36 合法 not_started），每 case 六轮、Draw 量、匿名资源引用、时钟、void HRESULT、NO_OVERWRITE 新范围、DEFAULT CB 整 48B 与常量写量/偏移全部通过。结果 `review-real-probe-raw.json`。
- Script regressions: Windows PowerShell 5.1 与 pwsh 完整回归各 225 项自然退出 0；最后仅涉及脚本的 Draw 分母/单位修补后，V2 定向各 100 项自然退出 0，真实 v1 副本/v2 单位定向均退出 0。冻结记录与用户文件哈希位于 script-freeze-final.json。不得将这些结果描述为真实 Win7 PS2 运行验证。
- Release x64/ARM64 完整 Solution 均自然退出 0（约 4m13s / 4m22s），各自 8 类早期 CLI 和 40 项真实离屏矩阵通过。最终冻结脚本的 ZIP 重打与 7 文件/manifest/全 ZIP stream SHA 核验由根会话交付，证据 packages.json；本检查未运行未知参数或操作常规发布 ZIP。

## 最终脚本核对与结论

- Package owner 按独立检查补齐 resources.csv / gpu.csv 的有界行解析、资源引用/类型/ID、六轮 GPU phase/iteration/status/marker/频率端点验证。CPU-only 的 36 行 not_started 保持合法；clear/copy/readback 的 ready 窗口不进入 Draw shader 证据。
- 中文报告已实际列出 HW/WARP 同 profile 数值与比值、40 case 冷/预热/测量/drain、最长单调用及资源角色、GPU ready Draw/全部 ready；不能只有状态及 CSV 链接。
- 最后的 complete 保护按 cold0/warm1/measure2..5 逐轮要求 raw Draw 数 = definition.draws_per_iteration = GPU.draw_calls。两个计数表按 case 重置；检查在加入成对比较之前。fake 补六条已测零时长 void Draw、总12行，保留预期比值2；缺 Draw/错 GPU count 的负夹具使矩阵 partial，不计算 pair。
- 已读 ps51-final-targeted.log / pwsh-final-targeted.log（各 V2 PASS100）、ps51-count-unit-final.json / pwsh-count-unit-final.json（均 passed、自然退出0、最终用户脚本哈希一致）。单位为 callback 批/帧，不再错误进入 ms 分布。
- 冻结脚本的真实 Debug ARM64、Release ARM64、Release x64 三套矩阵消费者均为 40 cases / 20 pairs / 0 issues，身份文件未修改。现有 native 40-case raw 与该 Draw 分母保护相容。
- 真实旧兆芯包只读副本可分析：schema1、graphics=not_collected_by_v1、169帧中26 Draw帧，9个空 GPU 样本和0个可证 Draw GPU样本。所有帧 P50约0.054ms与Draw帧P50约279.44ms分开，不能由空循环误判实际绘制。
- 最终源码检查通过，无遗留阻断项。保留上述元数据覆盖与目标设备未运行的明确边界；不再扩展生产逻辑或新设计。

最终交付复核：packages.json 当前两包均为最后冻结脚本；x64 Draw3Performance-v2-x64-20261015-072452-7d554992b51947d382885126b42e1491、ARM64 Draw3Performance-v2-arm64-20261015-072455-95c87de767e347e9815b521eaa030fc6。根会话 Verify-Packages 对 ZIP 每个 stream、manifest、源脚本/Release EXE SHA、PE 和精确7文件全部通过；检查角色再次独立核对两份 ZIP 的 SHA256 与五份冻结用户文件哈希，均一致。最终结论：PASS，无待修复阻断项。
