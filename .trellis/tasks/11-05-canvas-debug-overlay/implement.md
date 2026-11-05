# 实现计划:画布调试可视化模式

## 前置
- 先读 `.trellis/spec/native/runtime-and-rendering.md`、`native-desktop/{rendering-and-ui,input-and-ink,palm-eraser,configuration-i18n-and-assets,draw3-integration,canvas-freeze-boundaries}.md`,再动手(`trellis-before-dev`)。
- 实现前向用户确认一次:config 字段归属 `Inkeys::Config` 及分组名(规范门禁)。
- 原生文件 UTF-8 BOM + CRLF;i18n 只改 `src/i18n/zh-CN.jsonc` 后用 `Scripts/i18n.ps1 sync/check`。

## 步骤(按顺序,每步可独立构建)

1. **配置 + 设置开关 + i18n**
   - [ ] `Other.Config.cppm` 增加 `CanvasDebug` 布尔键(默认 false)
   - [ ] `Setting.cpp` tab9 增 ToggleRow、本地副本、`QueueConfigWrite`
   - [ ] zh-CN i18n 键 + `i18n.ps1 sync/check`
   - [ ] `IdtMain.cpp` 启动同步占位(先只存全局运行态)
2. **状态位 + 主栏按钮**
   - [ ] `IdtState` 增 `canvasDebugVisualActive` 与 `ToggleCanvasDebugVisualFromButton`、统一清零点
   - [ ] `Bar.Button.cpp`/`.cppm`:A1 运行时按钮(仿 Pierce/Freeze)、图标资源、`ApplyMainPresentationLocked` 状态、`Load()` 条件插入;核对白名单
   - [ ] 关闭开关时清零并隐藏
3. **Host → Draw3 通路**
   - [ ] `Host::SetCanvasDebugOptions`、WindowControl 原子、apply 循环、`DrawingController` 接口
   - [ ] 可视化模式下 Draw3 不出墨/不擦除/不触发手掌橡皮/不入历史
4. **采样与快照**(新建 `Draw3.CanvasDebug.*`)
   - [ ] 每帧 O(1) 计时累加,0.5s 结算 FPS / 无限制 FPS / 帧负载 / 进程 CPU
   - [ ] 120 点帧率环形缓冲
   - [ ] 活动接触槽表(采集点位于绘制线程读取接触处)
5. **背景板渲染 + 合成改造**
   - [ ] 板子内容绘制到缓存纹理(性能区、曲线、对齐表格)
   - [ ] 合成阶段 `dirtyOutside` 走 CopyResource、`dirtyInside` 走“板 + over L2”;pan 路径处理
   - [ ] 板子 prev ∪ cur 脏区并入 `frameDirty`
6. **可视化标记**
   - [ ] 渲染原语:点、圆框、虚/实矩形、文字
   - [ ] 压感缩放、面积 px/cm 文字、手掌橡皮阈值实/虚判定
   - [ ] 标记 bounds prev ∪ cur 并入 `frameDirty`
7. **验证**(`trellis-check`)
   - [ ] 全量 `InkeysRepo.sln` Debug|当前架构构建
   - [ ] 调试关闭时合成路径与脏区行为不变(对拍/日志)
   - [ ] 手测:触摸/笔/多点,面积红框虚实,压感,EDID 有无,手掌橡皮开关
   - [ ] 手测:板子区域内书写/擦除/翻页/pan 无残影;板子在 L2 之下
   - [ ] 点击其他工具退出可视化模式但板子保留;关开关全部消失
   - [ ] 性能对比:开启前后画布帧时间无明显劣化
8. **收尾**:`trellis-update-spec`(把“调试板在 L2 之下的合成/脏区契约”写入 `runtime-and-rendering.md`)、提交。

## 回滚点
- 步骤 1-2 独立于渲染,可单独保留;步骤 5 的合成改造是唯一触及主路径处,出问题时仅需关闭该分支(开关关闭即回原路径)。

## 风险文件
`Draw3.DrawingController.cpp`(合成/Present 前)、`Bar.Button.cpp`、`IdtState.cpp`、`Other.Config.cppm`。
