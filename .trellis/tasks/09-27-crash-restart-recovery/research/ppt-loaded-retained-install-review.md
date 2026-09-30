# F-038 活动 PPT 加载 retained map 独立复审

审查基线：H0 `8b156fca59f0337a6afc6d722941666fcf143080` 对当前工作树。只审 `Draw3.DrawingController.cpp/.cppm` 的 materialize、active/parked load completion、安装 helper、F-031 builder 与显式无 HWND CLI；同文件其它 Draw3 栅格/故障/保存补丁不借本报告宣称通过。本人未参与实施，未修改产品/测试、未构建、运行 CLI、启动 GUI 或采样性能。

## 结论与分级

| 等级 | 结论 | 证据与最小处理 |
| --- | --- | --- |
| F-038 当前补丁 | 未见其本身新增阻断性错误；原遗漏在既有门下静态闭合 | `MaterializePresentationSlot` 将 strict loaded snapshot 的 retained canvases 放入 `loaded->retainedSlides`（Controller `:768-909`）。新增 `InstallLoadedActivePresentationSlot` 仅在 `loaded` 有值且当前 `mutationRevision==0` 时，把 loaded document、page runtimes、retained map、file GUID 和三 revision 成组移至 active；当前 page/target 取同一次 `latestTarget`（`:912-946`）。生产 active completion 真调用 helper（`:6505-6535`）；F-031 `BuildPresentationSaveRequest` 之后读取 `activeRetainedSlides`（`:647-765,5640-5660`），不再从空/旧 map 保存。 |
| P1 / F-039 / 既有拓扑竞态，**不是 F-038 新回归** | 冷加载请求未决时 StableSlideId 删除或恢复页，旧 completion 用最新 target materialize 可丢页/重复页 | `CanReusePresentationDocumentSlot` 对相同 key/source 的两份合法 StableSlideId 列表返回 true，即使列表已变（`Draw3.Presentation.cpp:389-410`）；`SetPresentationTarget` 在 load pending 时仍能更新拓扑/target（Controller `:6560-6680`）；completion 随后取 latest target（`:6500-6515`）。现 `MaterializePresentationSlot` 的 `bySlideId` 只索引 `snapshot.activeCanvases`，只按 latest IDs 生成 active，retained map 只复制 `snapshot.retainedCanvases`（`:777-909`）。T1 active `{101,102}`→T2 active `{101}` 时，原 102 active 笔迹不进入 retained；T1 active `{101}`+retained `{102}`→T2 active `{101,102}` 时，102 变空 active 且仍在 retained。新 helper只忠实安装该 materialize 结果。最小修正为统一旧 active+retained 的 SlideID 值源，按最新 target 投影 active，剩余旧 active/retained 转 retained；重复 SlideID、不同 page GUID 冲突应 fail closed，EndScreen 单独处理。须先加下述 T1→T2 生产 helper 红测。 |
| P2 / 既有异常原子性条件，非本补丁首次引入 | 目标复制抛出后 active 可处于半安装状态 | `InstallLoadedActivePresentationSlot` 先移动 document/runtimes/retained，后执行 `active.presentationTarget = latestTarget`（`:935-944`），后者复制含字符串和 slideIds 的 target，内存分配失败可能抛异常。H0 inline active install 同样先移动 document/runtimes，再复制 target（H0 Controller `:5769-5780`）；F-038 新增 retained map 也在复制前移动。没有异常注入证据，不能称常态崩溃。最小先在任何 move 前构造 target 副本，再用无分配的 move/swap 提交整槽；失败时原 active/loaded 保持可用，并按原持久化错误门处理。 |

## Active、parked、mutation 与 F-031 值源

1. `PresentationPersistenceCompleted` 先以当前 `activeWorkspace==Presentation`、相同 key 和 `CanReusePresentationDocumentSlot` 判断 completion 是否属于 active；否则只查同 key 可复用 parked slot（Controller `:6350-6373`）。`PreviousInterval` 分支只替换匹配 pageGuid/ordinal 的单页，不调用 F-038 整槽安装（`:6420-6500`）。Current load 在 Loaded+snapshot 时以 `latestTarget` 调生产 `materializePresentationSlot`，失败返回空 `loaded`；active 分支先解除 loadPending、标 persistenceInitialized，再调用新增 helper；只有安装且 binding migration 时才推进 mutation 并调用常规 active 保存（`:6500-6540`）。因此迁移保存能读到刚装入的 retained map。
2. 新 mutation 门在 helper 最前，`loaded` 为空或当前 `mutationRevision != 0` 时无任何 active 字段写入；CLI 人工设 revision 9 并把新 retained 777 放入 active，断言拒绝后 workspace、map/revision 和 `lateLoaded` 均保留（Controller `:2267-2280`）。生产 caller 即使未安装仍清 loadPending 并尝试从**现有** active document 重建 GPU/ready；这是旧 completion 收敛行为，不能误称迟到文件覆盖了新笔迹。测试未故障注入复制/内存分配失败，见上表 P2。
3. parked completion 仍在同一 `loaded && parked->mutationRevision==0` 门后执行 `*parked = std::move(*loaded)`，retained 与 document/history/target/file/revisions 整槽转移；binding migration submit 用 `RetainedSlidesForSave(activeRetainedSlides, parked)`，选择 parked 自有 map（`:6540-6560`）。正常 A/B slot 切换的 `swapActiveDocument` 同时交换 document、runtimes、retained、target、file/revisions（`:5535-5551`），F-038 未改变这条路径。
4. F-031 的 builder/submit 已显式收 retained map：active `capturePresentationAutoSave` 传 `activeRetainedSlides`；parked Exit/迟到 completion 传 `RetainedSlidesForSave(...,&slot)`（`:5637-5723,6470-6498,6548-6556`）。F-038 修复 active map 的来源，使 F-031 同槽合同在**materialize 输入与当前 target 拓扑相容**时成立；F-039 说明仅搬运 map 不能替代 load 期间拓扑重投影。

## F-039 最小红测与冲突规则

- 用与当前 CLI 相同的**生产** `MaterializePresentationSlot → InstallLoadedActivePresentationSlot → BuildPresentationSaveRequest`，构造同 key/source StableSlideId、不同 page GUID 和笔迹首点。例一：T1 active slideIds `{101,102}`，旧 UInk active 102 的 pageGuid `G102`/笔迹 x=200、EndScreen `GE`；在 completion 前改 T2 `{101}`。预期安装后 `retainedSlides[102]` 保留 `G102`/x=200，builder retained 也保留且 active 只有 101+EndScreen；当前 materializer 应在此红灯。
- 例二：T1 active `{101}`，旧 UInk retained102 为 `G102`/x=200；完成前 T2 active `{101,102}`。预期 active102 恢复 `G102`/x=200，retained map 不含 102，builder 不生成同 SlideID 两份；当前 materializer 会生成空 active102+旧 retained102。输入里 active/retained 同 SlideID 或同 ID 不同 GUID 应直接拒绝，不允许 `std::map::emplace` 静默保留其中一份。当前 strict importer 对同一旧 UInk 的 duplicate_slide_id 已拒绝（`uink_draw3_import.cpp:338-407`），但它不能阻止 materialize 在**最新** target 上制造重复。
- EndScreen 无 SlideID，只在 active 文档尾部，绝不进入 retained；新插入且旧文件没有对应 SlideID 的真实页仍按现合同创建唯一空 page GUID。测试应断言旧加载结果被新 mutation 拒绝时任何 active 字段不变，再单列真正 Office/磁盘时序验证。

## 已有证据与未验证边界

- `RunPresentationLoadedRetainedInstallTest` 直接调用同翻译单元的生产 materializer、安装 helper 和 F-031 save builder（Controller `:2163-2280`）；它构造 active101、EndScreen、retained102 的不同 GUID/点，验证安装后的三 revision、active 页、后续 save retained GUID/笔迹，以及新 mutation 的迟到 load 拒绝。`IdtMain.cpp:416-417` 仅显式 `--draw3-loaded-retained-install-test` 在产品初始化前分派。该测试没有经过真实 Presentation worker 的 `ReadUInkFile`/strict import、Host/UI/Office、parked completion、T1→T2 拓扑竞态或异常分配失败。
- **red Debug ARM64 CLI exit 1 → green exit 0、完整 Debug Solution Build exit 0，以及 F-031/F-029/Laser/SuperTop 绿灯，均为主 agent 运行证据，不是本 reviewer 重跑。** 我只读原 red stderr 的 `post-load production save keeps retained page GUID and ink` 失败、现存 green stderr 的 `PASS: materialize-install-save identity` 及相邻回归日志；仅独立执行 Controller 两文件 `git diff --check`，exit 0。
- 真实 Office/WPS 多文稿、SlideID 交错/删除/恢复、EndScreen、old UInk/index 中已错属页、worker durable 写回/严格导入、设备 Present、Win7 SP1+仅 KB2670838 均未由本 CLI 验证。F-038 不能升级为跨进程 PPT 自动可见恢复已开放，也不能消除 F-026/F-027 图形故障退出问题。

复查命令：`git diff --unified=4 -- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cppm`；`git show HEAD:Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`；`rg -n 'MaterializePresentationSlot|InstallLoadedActivePresentationSlot|RetainedSlidesForSave|BuildPresentationSaveRequest|PresentationPersistenceCompleted' Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`；`git diff --check -- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cppm`。
