# 实施计划：脏区调试帧率显示改造

基线：HEAD `d6d40b0b`（分支 `refactoring/drawpad-ui`）。改动的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF；构建 `InkeysRepo.sln` `Debug|ARM64`；不提交，除非用户授权；不推送。工具：`%TEMP%\inkeys-codex\ui3\`（`build.ps1`、`fixfmt.py --check`、`runtests.sh`、`cmp-golden.sh`、`checkpoint.sh`）。

## 步骤

1. **缺省值**：`Other.Config.cppm` 的 `ShowFrameRate` 缺省改为 `false`，`Bar.Main.cppm` 的 `BarUiDebugFrameRateEnabled` 初值改为 `false`。
2. **实时帧率**：在 `UI3.FramePacing.cppm` 新增 `InstantFrameRate`；在 Headless `frame_pacing_tests.cpp` 增加单元测试，覆盖首帧无效、正常间隔、Reset、时钟回拨、工作耗时为 0。
3. **主栏接入**：`BarRenderLoopState::frameRate` 改用新类；修改帧尾采样与 `FrameRateUnavailable` 回退；新增 `debugFrameIndicatorSerial`，在提交点递增，在开关边界清零。
4. **排布与绘制**：
   - 按主栏中心计算文字与点的包络（含底栏吸附 Body 映射），替换原来的「主按钮下方」计算，包络并入内容范围；
   - 文字改为居中对齐，红/绿不透明；
   - 绘制 10 个点，跳过第 `serial % 10` 个。
5. **规范**：更新 `configuration-i18n-and-assets.md` 帧率合同（缺省值 `false`、实时计算、位置与颜色、删除「为采样维持的连续帧」），以及 `rendering-and-ui.md` 调试覆盖层条目。
6. **验证**：
   - `fixfmt.py --check`；
   - 构建 0 错误；
   - Headless 退出码 0（含新测试）；
   - golden 35 与基线一致；
   - `--ui3-attribute-dirty-test` 0/0/0；
   - host 38/0。
7. **实机检查（用户）**：
   - 删除配置项后默认关；
   - 开启后逐帧跳变、红字红点、缺点轮转；
   - 绿框帧变绿；
   - 展开/收起/动画/底栏吸附时居中；
   - 关闭后无残留。

## 回滚点

每步完成后 `checkpoint.sh dbgfps-<n>`；回退按文件从检查点取回，不使用 `git reset --hard`、`git clean` 或 stash。

## 进度记录

| 步骤 | 日期 | 结果 |
|---|---|---|
| 1–5 实现与规范 | 2026-11-19 | 完成（检查点 `dbgfps-1`）：缺省值 `false`；`UI3::InstantFrameRate` + Headless `TestInstantFrameRate`；主栏帧尾逐帧格式化、休眠恢复 `Reset()`、`debugFrameIndicatorSerial`（提交成功递增、开关切换清零）；文字+指示点整体居中于 `MainBar` 绘制矩形（Body 映射），颜色 `ResolveDebugFrameColor(IsPending)`；规范两处更新 |
| 6 验证 | 2026-11-19 | `fixfmt` 0 bad；构建 0 错误、无新增警告；Headless 退出码 0（无 FAIL）；host 38/0；golden 35/0（对 v10m）；属性脏区测试 0/0/0（自然帧 149、动画帧 206、未分类 0） |
| 7 实机检查 | 2026-11-19 | 用户确认全部通过，授权提交并结束任务 |
