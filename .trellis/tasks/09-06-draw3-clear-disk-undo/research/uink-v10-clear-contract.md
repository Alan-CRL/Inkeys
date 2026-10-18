# UInk version 10 Clear 合同

## 决策

- UInk version `10` 仍处于 Beta，尚未冻结；本次在同一 version 注册 Type ID `6`。
- Type ID `6` 名为 Clear，是前一个 Canvas 作用域内的 Map 内容块。
- 当前 Clear 只提供最简单语义：把同一 Canvas 此前全部可见合成结果重置为透明，同时保留旧内容供 Undo。
- 未来可以增加矩形范围及内容类型筛选，例如只清 Ink/Shape 而保留 Media；本次没有这些字段，缺失时固定表示 full Canvas/all visible content。

## Wire Shape

```text
UInkClear
  type: uint16 = 6
  contentId: uint32
  undoId: uint32
  extra?: Map
```

- Clear 与 Ink/Shape/Media 共享当前 Canvas 的连续 `contentId`。
- Clear 必须独占一个撤回组；位于其他内容之后时，其 `undoId` 必须大于前一组。
- Clear 没有几何、颜色、viewport、page 或 layer 字段，作用域完全来自前一个 Canvas。

## Rendering And Undo

对象流 `A, Clear1, B, Clear2, C` 当前只显示 C：

- forward composition 遇到 Clear 时把当前 Canvas 结果重置为透明；
- Undo C 后为空，Undo Clear2 后恢复 B；继续 Undo B、Clear1 后依次为空、A；
- 仍有效但被后续 Clear 隐藏的旧区间必须由完整保存保留；
- 真正撤回的尾部内容/Clear 可以在完整保存时移除并重新编号；UInk 不承诺保存 Redo。

`renderOnlyWhenLatest` 的反向扫描在 Clear 处停止，不能把上一区间的标记内容并入当前区间末尾组。

## Scope And Compatibility

- Clear 只影响所属 Canvas，不影响其他 Device、layer、page 或 Workspace，也不改变 Canvas identity/viewport。
- 当前 full Clear 包括 Ink、Shape 和 Media。
- 多 layer 是同时合成的图层，不能用 layer 模拟互斥历史区间。
- 早期 version 10 reader 会把未知 Clear 跳过并错误叠加旧区间；最新 Beta 规范明确这些实现不属于当前 version 10 兼容基线，旧草案不提供迁移保证。

## Incremental Save

- Clear 只可追加到文件末尾最后一个 Canvas，使用连续 contentId 和新的独占 undoId。
- 目标是旧 Canvas、撤回/重做 Clear、修改旧区间或压缩历史时必须完整保存。
- PPT 任意页 Clear 通常不是文件末尾 Canvas，因此产品 canonical save 采用完整重写；worker 合并磁盘中的 sealed intervals 与 controller 提交的 active tail。

## Applied Documentation

已在 `D:\Project\Inkeys\website` 修改且 `pnpm docs:build` 通过：

- `docs/standard/version.md`
- `docs/standard/type.md`
- `docs/standard/blocks/clear.md`
- `docs/standard/blocks/canvas.md`
- `docs/standard/blocks/ink.md`
- `docs/standard/blocks/shape.md`
- `docs/standard/blocks/media.md`
- `docs/standard/file/main.md`
- `docs/standard/incremental.md`
- `docs/standard/intro.md`
- `docs/standard/conformance.md`
- `docs/.vuepress/plume.config.ts`

website 工作树保持未提交；没有生成或修改 public fixture。

## Inkeys Implementation Impact

- UInk model/codec/encoder/append variant 增加 `UInkClear`，reader normalization、limit charging、unknown provenance 与 tests 同步。
- Draw3 export/import snapshot 从纯 Stroke 列表升级为保持顺序的 Stroke/Clear 操作或等价 sealed-interval + active-tail 表达。
- Desktop 现有单区间历史文件可以继续不包含 Clear。
- Presentation canonical UInk 保存每个 Canvas 的全部有效 Clear 区间；controller 只保留 active interval，worker 从 canonical 文件合并 sealed history。
- Presentation 普通 active-tail save 可以在同 interval latest-wins；Clear boundary 必须按 PresentationKey/page/interval 顺序 durable，不得被合并丢失。
