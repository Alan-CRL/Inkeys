# UI3 F Root observer / DPI / BGRA 接线交付（待整体构建与 safety）

Active task: .trellis/tasks/09-27-integration-and-release-check

2026-10-01。冻结依据为独立 Source 设计45AA6DB4…05A7C8与 Auth scoped approval；本报告不升级整体F运行许可。Root拥有五源，Source实现者单独拥有Source/Test/Interaction/Button，Draw3 writer拥有Host与HiddenTest。无commit/GUI/ComputerUse/新权限开关。

## 实际最小接线

- A：observer显式Enable仅在pub0/无请求、无frame/owner/ready、合法完整已seed签名前；仅实际初始Complete的同候选settled/完整四API/有效anchor路径私有Freeze。仅Fit的configZoom可变，色宽工具/theme/DPI/display不吸收外来变化；冻结通过ready release及Interact线程创建建立先行关系。RenderLoop Submit后若本帧旧zoom不同新barStyle.zoom，初始finite观察记原InitialPending，等待Fit自己既有Request的下一自然帧，不改绘制/再Submit/补帧。
- B：第一BeginFrame的实际thread id一次发布，copy首先用atomic owner gate拒错线程，再读owner私有prefix最多512行。完整run/step/source/revision/scene、真实Completed/role/resource/epoch/surface核后复制原首次完整row，不从易被后帧推进的ready拼QPC；NoChange引用不生成新完成tuple。保留历史failureFlags，不把正常曾pending误认为当前未完成；封口后拒copy。
- C：普通同步render-owner-only函数先核exact Auth/本地EquivalenceReady和B的原完整row，再读coordinator。每次真实BeginDraw撤销backing读回资格，仅完整四API成功后锁存本次实际goal/epoch/surface/attempt/ULW source/viewport/alpha/mutation/invalidation。功能C允许同goal后续真正完整成功帧，原MeasuredEnd行/时间和当前图的成功attempt分别保存。失败/deferred后写、epoch/surface/alpha变化或未成功提交拒旧图。
- C从实际target取得同owner局部COM lease，仅复制完整ULW输出source rect；BGRA premultiplied格式无转换、不裁掉目标/缩图/新增绘制或Present。destination≤16MiB/readable逻辑与真实Map pitch*height≤16MiB，合计功能≤32MiB；Source先核fixed/raw/scratch共同64MiB。Unmap/局部COM释放在Source单job完成回执前。CPU_READ+CANNOT_DRAW只用于一次EquivalenceAfterRun，不在计时段每帧读回。
- Main复用正式EnsureProcessDpiAwareness的可信系统DLL/Win7 fallback，首先核Auth；新early dispatcher在配置/单实例/普通UI之前返回。新Host smoke selector仅准确参数 --draw3-host-metrics-smoke --output-root，实际函数另writer实现，独立safety前不运行。Auth两ordinary实际链接定义及工程登记待Source停写，不用stub。

## 已执行与未验证

- 独立Headless项目 ui3-f-ab-helper-debug-arm64-build实际0；正确项目输出 --no-window pid34412自然0，新增F201–F204直接调用生产observer，核初始旧zoom Pending/失败API/缺anchor不freeze、下一真实一致签名freeze一次、首次accepted用新基线、原构造行为、外来色宽拒绝、首次完整row保留、错tuple/非owner/NoChange/Seal拒证。它们只证明数值合同，不是真实Fit/GDI/Bar GUI。一个新局部first shadow warning已改firstCompleted纯命名，下一完整构建复验；旧链接warning不修改。
- 两次插入测试脚本assert因为预算static_assert锚出现两处而失败，没有写源码/执行新测试；随后准确末B222范围插入，现0来自实际F201–F204实现。没有把脚本错误作为因果RED。
- C、Main、真实Fit producer/new Auth/Source/Host尚未完整主Solution构建或窗口测试；当前没有wholeF PASS/性能/Win7/HC-H2数字。精确全BGRA/same-size recreate/失败写/预算/partial-init/job晚到与全部真join仍待独立实码和实际验证。
- C用同期成功typed元数据而非requested/ready拼接；普通defaultnull不读新clock/分配Session/job/执行Copy，coordinator新增固定小POD不是全进程内存零差。所有借用必须持活到control job真完成和Scheduler真join；Unregister只覆盖render callback，不代替job静止。Root不在本函数内二次PostControl/等待，Source负责唯一owning job及原Close15保护。

## 官方API口径（2026-10-01核）

Map要求CPU_READ，CPU_READ与CANNOT_DRAW共用；文档列Platform Update for Windows7。CopyFromBitmap按像素区域复制，不做格式转换，当前batch可能内部flush，功能费用在统计段外，不新增显式Flush。Win7 SP1仅KB2670838目标仍待真机，本API记录不推导实测通过；FLIP/两DWMgate不变。

- https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1bitmap1-map
- https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1bitmap-copyfrombitmap
- https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/ne-d2d1_1-d2d1_bitmap_options

## 当前冻结源身份

| 文件 | SHA256 |
| --- | --- |
| Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h | 4B82819DF7D37557CA020FB19FA7ED38912AC9DFF507190464F88CE16F512793 |
| Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.cpp | 0538C544E77749E57017B36783A23BA9437BC3AA0BBA28CC7208009C1F5B45A8 |
| Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp | 520190BD6FCB18D7F68E7B5EAE2214FB79C3CCD3C7CA867EF9F1C9E42D382EC4 |
| InkeysHeadlessTests/render_scheduler_tests.cpp | 49A6585EADCFF9898629037C2751C111C6BC2F06D7F2EA95656CE5C0D37CAD45 |
| Inkeys/IdtMain.cpp | 00C13D99A4F73915F8375AD38FB14DB6B5AFBADAF971855D9826C2F191FE70C3 |

## 独立review后三项最小修正（未全构建）

- 新UI3 dispatcher先接，再旧SS，不修改旧SS purpose/协议。
- Host smoke按CommandLineToArgvW后的exact argv1/argc4识别，mode加引号也early接，已识别坏形状2，不落普通GUI。动态引用/坏参矩阵待新主Solution。
- 私有render state最后成功target ComPtr lease+当前指针匹配，拒same epoch/size重建且尚未BeginBackingWrite的窗口，并防same-address ABA。新BeginDraw撤Available/Resetlease，完整四API后持当前target；normal null无新增AddRef/图拷贝，私有on/off同门，空成员固定小尺寸及可能额外旧GPU资源暂存如实计入资源观测，不当CPU payload零内存。
