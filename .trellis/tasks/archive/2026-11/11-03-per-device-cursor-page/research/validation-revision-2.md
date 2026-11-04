# Revision 2 最终验证（2026-11-04）

## 结果

- 完整 `InkeysRepo.sln Debug|ARM64`，动态定位 ARM64 原生 MSBuild，使用既有 PATH 规范化与10分钟上限，最终退出0、0 error。
- Arrow实际GPU：`--draw3-renderer-upload-test` 退出0。正式/完整guarded PS在192DPI中段均为一列不透明黑边BGRA(0,0,0,255)，紧邻像素为不透明白填充(255,255,255,255)，外框仍25×39、热点不变。其余96/144/264 DPI输出也通过；没有改shader。
- `--draw3-pen-width-test` 退出0，尺寸和当前DPI生产接线回归通过。
- `INKEYS_SETTING_POPUP_ONLY=1 InkeysHeadlessTests.exe --no-window` 退出0，`PASS animation correctness`。仅设置部分聚焦Popup，其余算法/配置/输入/Bridge/contact等仍运行；Arrow倍率/触摸回归、普通/scoped弹层首帧上下避让、resize、受限滚动、键盘、鼠标选项与退出动画均通过。覆盖既有Combo交互Solid/Acrylic和DragScrolling长列表。
- 光标页专项（cursor-only）退出0：20语言×1/1.5/1.875 DPI×窄/宽布局；每种语言的实际RenderCursorPage在宽布局滚到底后，上开并鼠标选择最后一项，且只产生一次事件。
- `--no-window --setting-general-only` 退出0，现有通用设置/PPT消费者回归通过。
- i18n check退出0，20语言730/730，本轮无新文案、不改配置格式。
- 40项修改跟踪文件的原始文件系统字节备份对比UTF-8/BOM/EOL保持；git diff --check通过。最终源码身份及二进制SHA见Build/cursor-revision2/source-state.json、validation.json。

## 范围与环境

用户已确认箭头大小正确，本轮仅将Arrow边宽改为max(1物理px,0.5 DIP×scale)，200%为1px；尺寸13×19.5 DIP、热点和其它样式不变，设置预览共用规则。共享Combo沿当前viewport选择下方或上方；普通和motion路径都首帧确定尺寸，双方不足则在较大侧限高滚动，保留原Popup与输入/动效，无新增原生窗口。

第一轮构建的首个源码错误为新增测试lambda与旧firstOpen局部重名；已只重命名新增lambda及三处调用为openFirstFrame，并将新增点击点命名optionPoint消除本轮新增遮蔽警告。相同solution增量重编通过，未改产品逻辑来绕过测试错误。其余既有第三方/旧测试warning未在本轮清理。

复用同任务已验证的沙箱外PDB/原子写盘环境；构建前已确认绝对路径在工作区内，仅将可再生成的主程序增量链接缓存留存到忽略的验证目录，未改SDK、项目或增量链接配置。首轮R1已跑过未过滤完整无窗口集，本轮按影响范围执行上述定向设置与完整其它无窗口回归，没有重跑无关HomeWARP性能基准，不把过滤运行说成完整设置集。

## 验收边界

没有启动主窗口、设置窗口或Computer Use；真实桌面细边观感与底部上开行为仍由用户回核。当前Windows离屏guarded PS通过不等于真实Win7验收。没有commit/push/归档；任务保持in_progress。

## 共享消费者最终闭环

全量产品入口审计确认教学安全、光标、PPT闲置时长、快捷方式预设等均通过同一Design::ComboBox；没有页面位置特判或绕过的直接ImGui/ImFluent下拉。新增真实RenderGeneral(Interaction)教学安全位于客户区底部的首帧上开/鼠标末项单事件回归，已在general-only的20语言×3DPI全部通过。新增用例后完整solution再次退出0，五项受影响无窗口检查再次全部退出0。产品Inkeys.exe SHA与补用例前完全相同，证明本次仅补验证，共享产品实现未变。40项源码身份保持。没有GUI、commit、push或归档。
