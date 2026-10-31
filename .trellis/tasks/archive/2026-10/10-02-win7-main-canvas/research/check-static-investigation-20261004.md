# Win7 晚间实测前静态调查独立审查 — 2026-10-04

Active task: `.trellis/tasks/10-02-win7-main-canvas`。

审查结论：**限定范围 PASS**。四份专项研究及汇总的证据、因果顺序与假设优先级一致；没有发现可静态证明当前透明读回唯一根因的生产缺陷。本 reviewer 只写本报告，产品源码、脚本、CSO、候选与现场包只读；没有编译、GPU运行、HWND、GUI、commit、push或归档。

审查输入：`demo-product-ulw-static-20261004.md`、`dxgi-flip-boundary-static-20261004.md`、`copy-ulw-lifecycle-static-20261004.md`、`shader-win7-contracts-static-20261004.md`、`static-investigation-summary-20261004.md`。先读取本任务check.jsonl、PRD/design/implement；沿用前一轮已完成的同帧输入/光标/pixel研究，不重复整条输入或几何推导。

## Findings (fixed)

- File: `research/shader-win7-contracts-static-20261004.md`
- Issue: 开头“现场EXE、当前候选、当前四个CSO完全同源”可能被理解为全部C++源码/整个EXE身份相同，超出了SHADER资源读取的证明范围。
- Fix: 已通知主会话，将文字收紧为“两EXE嵌入的四个shader分别与当前对应CSO全字节相同”。主会话完成修正；reviewer重新读取确认。汇总也明确证明限shader字节。没有改产品。

## Findings (not fixed)

- 原Demo的用户已测Win7二进制身份仍未建立：当前原仓ref及本次读取的核心文件可追溯，但不能把它们或已有EXE名称/时间等同于当年成功的Win7 EXE/driver/ULW模式。尊重用户实测，不以本地旧TestResults的Hardware/DComp/DWM否定它。
- Win7 corrected selfprobe仍待现场结果：现有22:48采集只有正常应用输入/绑定/全零staging，独立六例因PS2参数冲突未启动。此前Win11 ARM64/x64修正版成功不能充当Win7通过；本轮静态审查没有新增运行通过结论。
- 两项生命周期观察没有本次因果证据：释放引用之后缺少显式Flush可能影响同HWND替换链；DIB在尺寸变化时重建却沿用partial可能短暂丢区域。当前普通链成功、稳定尺寸、无对应恢复失败，不足以解释持续全零source。维持只记录，不改今晚候选。
- dynamic structured CPU_WRITE/MISC文档备注与具体资源规范有歧义。创建/Map成功不证明GPU读取字节正确，也不能凭概括备注宣布Win7必定非法。相关11.2条文不能移植成Win7能力保证；报告已明确此界限。
- 唯一GPU根因和桌面恢复仍未证明。现有高优先级范围是GPU数据/执行/输出/正式混合及backbuffer→staging拷贝读回；不得把它们写成已经发生的某个确定驱动或runtime缺陷。

## 关键证据复核

| 关键结论 | reviewer 独立复核及界限 |
| --- | --- |
| 本仓Demo核心与原仓对应文件相同 | 只读bytes/SHA验证graphics_initialization、transparent_presentation、renderer、renderer_primitives和main.cpp五项全部相同，hash逐项匹配专项表；这不证明原仓git-clean或当年已测EXE身份 |
| 产品vs当前Demo shader执行内容相同 | 只读解析DXBC四个CSO，SHEX/ISGN/OSGN/STAT逐项全部相同；RDEF仅offset21不同，flags0x100/0x8100；产品hash匹配报告四项。容器hash差异不能直接当执行指令漂移 |
| 产品vs原Demo有明确PS差异 | 四CSO比较中原PS仅OSGN与产品相同，其余执行/输入/统计块不同；VS与两个CS各块均相同。未扩大“当前Demo相同”为“原Demo所有shader都相同” |
| 实际故障EXE嵌入shader身份 | 专项只用LOAD_LIBRARY_AS_DATAFILE核旧06CD58和新2D468B的SHADER301–304，并给出长度与hash；reviewer采用此只读资源证据，不再次加载EXE。它不执行产品，也不证明全部C++源码身份 |
| 原始source统计先于USER32 | 独立读CopyAndInspectUlwDirtyRows及Present：统计读取mapped staging的sourceRow，CPUunderlay只写DIB destination；afterCopy Unmap后才调用UpdateLayeredWindowIndirect。返回后打印的是已累计对象，无重新读源。纯ULW/DIB/Z序故障不能倒过来将这些source统计变零 |
| 当前ULW不执行Present1 | 采用现有源码/现场GetDesc1研究；有FLIP对象不等于已验证FLIP→DWM直接呈现。仍需核交换链backbuffer作为资源的Clear/Draw/Copy行为，不能从Windows版本标签替换backend |
| Map/Flush及源RTV绑定 | 独立核Microsoft合同：Map是runtime自动提交时机；Flush异步且非完成证明。Copy合同约束资源类型/格式、边界与未mapped，未提出源RTV必须解绑。报告没有把缺Flush/解绑静态定为根因 |
| 所有权、恢复与瞬态draw | 接受专项同一Draw3 immediate-context/owner审计，未重查整个controller。Cursor与LaserTip共用t0/b0/VS/PS/blend且绕过history/L1/L0，支持共享区间优先；绑定正确不证明buffer内容或shader输出正确 |
| 销毁顺序风险 | 独立读ReleaseSizeDependentResources/ReleaseAttempt：Flush确在backbuffer reset/swapchain reset之前；Microsoft替换链销毁合同有引用释放后Flush的要求。报告保持“风险/未触发”，没有把它提升为现有链全零根因 |

## 优先级与晚间六例的含义

1. 两类Clear建立颜色读回正反证据，优先区分普通纹理与swapchain-backed资源及Copy/readback。
2. Clear有颜色而正式Cursor失败，再看实际VS/PS计数和两个关闭混合对照；这提升draw专属数据/执行/blend/写入可见性的优先级，不等于百分之百排除所有Copy或同步差异。比较态成功不是永久禁用blend的授权或墨迹语义验证。
3. 六例全部PASS仅覆盖128×128 Circle/fullCopy/显式同步；生产EraserGrip shape6、1920×1080、非原点dirtyCopy、保留RTV/阻塞Map的差异仍待需要时单变量验证。
4. source/DIB非零而桌面不可见后，USER32/窗口最后一步才成为直接解释。不能先用纯CPU填色ULW代替缺失GPU证据，也不要求重新重复RTS/controller已证明的输入序列。

这些是因果判别次序，不是已证实故障的概率估计，也不能保证第一轮六例一定给出唯一原因。当前沿用已交付修正包，不增加生产或候选版本。

## Verification

- Lint: PASS — 本轮最终`git diff --check`退出0；新增review报告UTF-8/CRLF。
- TypeCheck / Build: 未运行；用户限定静态调查，产品无本轮修改，不重复前一轮已完成完整Solution构建。
- Tests: 静态校验PASS — 五个来源文件bytes/hash、四个DXBC的执行/签名/metadata块与报告一致。未执行产品或GPU测试；晚间Win7 corrected probe仍待用户。
- Specs: 主会话拟在已有input-and-ink七节合同内追加两条记录：source统计保存于ULW之前、ULW读取backbuffer与Present1直呈分开。它们与已核代码一致，无新产品入口/资源/项目/模板触点。
- 保留证明：主会话报告baseline hash断言PASS：原IdtMain/collector/launcher、8个CSO、已交付ZIP均未变；产品delta仍仅为上一轮IdtMain入口修补，task保持in_progress。本reviewer没有重复该全团队hash检查，其独立bytes/DXBC校验均只读；源码与候选保留结论归主会话实际断言。

## 官方合同复核链接

[Microsoft Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)说明runtime可在Map时提交、Flush异步及flip替换链的deferred destruction；[CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)说明拷贝资源/坐标/未mapped约束。它们不证明本机或Win7某个Draw有有效颜色输出。

[DXGI_SWAP_EFFECT](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect)声明D3D11 FLIP_SEQUENTIAL从Windows8支持；[CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd)包含Platform Update for Windows7及不支持ScalingNONE的边界。公开声明与现场创建/GetDesc1事实同时保留，不能据其中之一否定另一证据或承诺所有Win7链都正常。

[CPU_ACCESS_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_cpu_access_flag)、[RESOURCE_MISC_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag)的泛化备注和[功能规范](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)的具体描述须保持版本和适用范围；本review未从11.2例外推导Win7能力，也未将文档歧义升级为已复现失败。
