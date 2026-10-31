# F-039 PPT 冷加载期间稳定页拓扑变化

活动 Presentation 处于旧 target `{101,102}` 的异步 load pending 时，Office 可把同 key 的 StableSlideId 拓扑更新为 `{101}`。当前 `SetPresentationTarget` 会 Rebind 到新 target，旧 completion 经 `CanReusePresentationDocumentSlot`（核 key/source 与各列表合法，不要求相同）仍可被接受。`MaterializePresentationSlot` 按最新 target 只把旧 active 101 装入 active 页，却只从旧 `snapshot.retainedCanvases` 建 retained map；旧 active 102 未成为 retained，已提交笔迹可能在下一次保存覆盖时丢失。条件性真实 Office 交错尚未运行，静态调用链由独立 review 确认。

修补必须保持源 snapshot、最新 target、同一文稿 key 和页身份：旧 active 中不在最新 StableSlideId 列表的正 SlideID，应在 materialize 时以原 page GUID/笔迹/interval 等完整 Canvas 身份进入 retained；已存在 retained 同 SlideID 时核冲突，不能静默覆盖或串入另一文稿。EndScreen/末页明确单列，不能把结束页当已删普通页。新 mutation/旧 completion 的拒绝门保留；合法迁移和严格 UInk worker schema 不改。

先从同一生产 `MaterializePresentationSlot` 做无 HWND T1→T2 确定性红测：旧 active 101/102 各不同 GUID/点，最新 target 仅101，完成后 101 active、102 retained 且再次经生产 builder 保持内容；旧 retained 与新迁入同 SlideID 冲突、重复/非法 SlideID、EndScreen 分开测。Stage2 最小实现转绿，完整 ARM64 Solution 与 PPT 同源/F-038 回归、独立 diff review。真实 Office 删除/重排中加载/多文稿/Win7 仍人工。

对称的新增页也需覆盖：旧 snapshot active `{101}`、retained `{102:旧笔迹}`，最新 target active `{101,102}`。当前 `bySlideId` 只索引旧 active，102 会变空 active，同时 retained map 仍含 102，可能生成 duplicate_slide_id 并令严格导入拒绝。设计上先把同一 snapshot 的 active+retained 统一按 SlideID 核验来源/重复/页 GUID，再按最新 target 投影 active；未被选作 active 的已知普通页成为 retained。102 必须恢复原 GUID/笔迹且不再同时留在 retained。EndScreen 独立，不能参与普通 SlideID map。
