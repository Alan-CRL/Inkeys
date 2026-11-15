# 第六轮实现与验证

## 交付状态
- 子任务：11-14-shape-round6-interaction，父任务：11-05-shape-ux-rework，均保持 in_progress。
- 已批准八项需求实施完成；单指平移，多指平移/等比缩放/平面旋转，3D 不接纳多指旋转。结束编辑才恢复原工具，未完成构造仍可继续。
- dev c754e200 已合并，合并提交 a5e6f85f；该提交只包含上游与冲突解决，第六轮功能/测试/文档随后按用户要求提交，未推送或归档。
- 额外修复 dev 停止消息路由遗漏，具体证据见 dev-stop-routing.md；原始保护和身份校验不变。

## 自动验证结果
| 项目 | 最终结果 | 日志（Build/shape-round6） |
| --- | --- | --- |
| 共享完整 solution Debug ARM64 | 退出 0 | core-build-final.log |
| 主程序完整 InkeysRepo.sln Debug ARM64 | 退出 0 | product-build-stop-route.log |
| shape-core-only / uink-file-only | 均 0 | core-results-final.json |
| 产品形状核心、工具状态、工具恢复、input worker | 均 0 | product-results.json |
| 白板漫游 / 100 页存储及冷撤回恢复 | 均 0 | product-results.json |
| Headless --no-window / 自动保存 | 均 0 | supplemental-results.json |
| 形状编辑器离屏 | 544 场景 / 4206 呈现提交 / 240655 检查 / 0 失败 | editor-ui.log；Build/shape-p34-ui/results.log |
| 几何属性离屏 | 1586796 检查 / 144 帧 / 0 失败 | geometry-ui-final.log；geometry-final-result.json |
| 形状 renderer | 471 项 / 0 失败 | shape-renderer-final.log |
| 原生输入路由（无 HWND） | 37 项 / 0 失败 | native-input-final.log |
| 隐藏焦点与真实停机 | 退出 0，SF48 停前/停后确认 | focus-hidden-stop-route.log；focus-stop-result.json |

## 审查与修复记录
- 独立全范围审查：预测在真实停顿后重新建立；重复时间不永久禁用；新增手指先撤去旧预测，再锁存新基准；构造中预测几何与圆形控制点同帧更新，真实点列不受影响。
- 鼠标移出不取消捕获中的拖动；失捕/隐藏/触摸解码失败按真实输入会话取消。接触 source/id 分离，末指 Up 才提交一次 Replace，压力不参与几何或样式。
- 浮条实际粗细按 transform 行列式尺度换算，等比缩放不改变渲染线宽。二次翻转还原，全部 21 个预设与立体 v7 保存重载有回归。
- 几何面板旧断言始终计入参数行，按新跨分类隐藏合同修正；其他尺寸、内容、命中及像素断言保留。
- dev 的 Laser 按需资源策略要求夹具调用 EnsureLaserResources，非空与像素基线未放宽。
- 隐藏测试 editorOpen 原先错误接纳构造预览，修正等待条件避免提前开始下一笔；功能断言通过后又发现真正的 dev 停止路由遗漏，已最小修复并独立审查。

## 环境与人工验收边界
- 共享首轮受限 runner 停滞及 UInk NTFS ACL 探针差异，在正常权限下使用同一构建/测试命令通过；无源码规避。
- MSVC ARM64 增量链接多次在 IncrBuildImage 内部报 LNK1000/C0000005；仅清理工作区生成的 Inkeys.ilk 后完整链接成功，未修改工具链、SDK或工程配置。
- PATH workaround 后既有附属步骤提示 pwsh.exe 不可见，完整构建最终退出 0。保留既有 warning，未为零 warning 修改无关代码。
- 未启动交互式 GUI、未使用 Computer Use。离屏测试读取实际像素；隐藏 fixture 使用不可见测试窗口并自动退出。真实设备触控手感、预测观感、鼠标/笔实际捕获仍待用户验收。
- diff --check、BOM/CRLF/混合换行检查完成，见 format-audit.json；只清除了本轮生成的四个未跟踪 shader CSO 和临时几何测试程序副本。

最终程序：Build/ARM64/Debug/Inkeys.exe
SHA256：ACF15378A591B540CA02C22DA327E29B5497392BF20F0642499CAC9F8C84D287
