# UI3 Bar 按钮布局配置

## Goal

为 `Inkeys.Other.Config` 增加可复用的顺序序列持久化能力，并用它保存 UI3 Bar 的按钮 ID、顺序和用户可见状态，使旧配置保持现有界面，同时为后续设置界面、插件和组件按钮预留稳定接入点。

## Requirements

- 配置层必须能够将带读写锁的顺序序列保存为 JSON 数组，并在完整解析成功后事务式替换内存数据。
- 数组元素必须支持独立 codec；首个元素为 `{ "Id": string, "Visible": bool }`。
- 配置路径为 `UI.Bar.ButtonLayout`。缺少字段或字段无效时使用当前主栏默认布局；`Visible` 缺省为 `true`。
- 默认顺序为 Select、Draw、Eraser、Geometry、Recall、Clean、Divider、Pierce、Freeze、Setting，其中 Geometry 默认隐藏；Redo 不进入配置。
- 官方按钮 ID 必须是稳定的 `Inkeys.Bar.*` 字符串，并在注册时声明是否允许重复；当前仅 Divider 允许重复。
- 不允许重复的按钮采用第一条有效记录，后续重复项从内存配置中移除，并在下一次正常写入时从磁盘清理。
- 未注册 ID 及其重复记录必须原样保留但不创建 UI，确保插件恢复后布局不丢失。
- 用户可见状态必须与现有上下文 `hide` 状态分离；有效可见性同时满足用户可见和上下文未隐藏。
- 本任务只实现配置、注册和启动加载，不实现设置界面的排序/显隐编辑。

## Acceptance Criteria

- [ ] 旧 `main.json` 没有 `ButtonLayout` 时，启动后生成默认数组，界面显示与当前一致。
- [ ] 调整数组顺序和 `Visible` 后，UI3 Bar 按顺序加载并正确隐藏按钮。
- [ ] 多个 Divider 均可加载；不可重复按钮只加载第一项，后续重复项在下一次配置写入时消失。
- [ ] 未注册插件 ID 保留在配置中且不渲染。
- [ ] 无效元素导致整个布局字段回退默认值，不留下部分加载结果。
- [ ] `configOnce = config` 能正确复制序列值。
- [ ] `git diff --check` 通过，`InkeysRepo.sln` 的 `Debug|ARM64` 完整构建成功。

## Notes

- 目标分支为 `feature/settings`。
- 当前 `feature/animation` 的 UI3 粗细改动已在 `e85505d` 独立提交，不合入本任务。
