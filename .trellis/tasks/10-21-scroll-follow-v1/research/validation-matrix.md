# 最终验收矩阵（持续更新）

证据必须来自最终对应实现。仅编译、回调计数或 Win32 API 返回成功不能替代真实像素、Draw3 呈现或存储核对。尚未执行的阶段不得标记完成。

| 阶段 | 项目 | 当前状态 / 证据 |
| --- | --- | --- |
| 基线 | 已有无窗口测试 | 已通过：使用仓库 Build/ScrollFollow/TestTemp；详见 baseline-validation.md |
| 捕获 | Debug ARM64 完整 solution | 最终check完整solution exit0，8.68s、0 warning/0 error；代码审查已完成 |
| 捕获 | 不可见内容仍连续变化 | 独立冷启动两轮已通过真实源匹配、连续变化和完整排除；最终2880×1920两轮平均54.523/64.991ms、10.302/8.491FPS。bare-lease与真实隐藏first-owner/global-once均130帧Filter/error50；审查cold另有55帧NearBlack/0有效。历史通过不能覆盖当前失败，门槛未放行 |
| 捕获 | 窗口排除、重复启停、取消/资源 | 历史cold/lifecycle exit0。审查lifecycle总体exit1：失败/回调异常/1秒监督/Stop/busy/lease保活/清理通过，末尾有效像素再启失败。未注入真实PrintWindow永久阻塞 |
| Tracker | 静止300帧、长距离/接力/反向/亚像素 | 未进入；依赖捕获真实门槛 |
| Tracker | 固定UI/动态干扰/缩放旋转/低信息拒绝 | 未进入 |
| 会话 | absolute latest-only、seq/session/scene拒绝、End最终屏障 | 未进入 |
| Draw3 | 接触收尾、辅助窗ready、Tile恢复、Resize/设备失效、钳制 | 未进入 |
| 产品 | More按钮、中英繁/i18n资源、工具/Freeze/工作区互斥 | 未进入 |
| 存储 | A→跟随→B→编辑历史→保存/读回→再次跟随 | 未进入 |
| 回归 | 相关Draw3/UInk CLI、形状/Freeze/白板/PPT | 未进入 |
| 构建 | 最终ARM64与x64、PE导入 | 首次x64依赖成功/Headless链接成功，产品C1060 compiler heap失败；唯一命令级CL_MPCount=1重试11:44.66后产品Inkeys.exe成功链接，但Headless链接LNK1140/PDB上限且旧测试exe被删除，完整solution未通过、x64测试未运行。ARM64/x64产品PE导入通过静态检查；旧Windows未实跑 |
| 静态 | i18n、diff、编码/BOM/EOL | i18n en-US/zh-TW各425/425、diff/XML/原BOM与CRLF/新源码UTF-8无BOM及CRLF均通过；任务记录收尾继续保持原格式 |
| 受控GUI | 专用内容、可用浏览器/Word/PDF、上下/水平/惯性/暂停退出 | 专用capture/lifecycle已测试（shared门槛失败）；产品矩阵尚未进入 |
| 真机补测 | 触摸/笔、旧Windows、未测应用场景 | 最终报告逐项列出；不能由组件检查推定通过 |

## 实际证据位置

- 构建与测试完整日志放在 gitignored 的 Build/ScrollFollow/。
- 可追溯的结论、命令、退出代码与统计保存在此 research/ 和各子任务 implement.md。
- 只有捕获窗口不可见内容、真实源变化、完整排除表与重复启停全部通过后，才启动 tracker 子任务。
