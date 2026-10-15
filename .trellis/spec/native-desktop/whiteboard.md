# Whiteboard 初版合同

本合同来源于 2026-10-15 用户批准的 Whiteboard 初版计划，覆盖白板、共享分页、Draw3 页级存储及主栏场景生命周期；实现与验证记录见 `.trellis/tasks/10-15-whiteboard-v1/`。此前 08-17 白板任务中的无保存、统一墨绿背景、退出强制折叠和左右同宽约束由本合同替代。

## UI 与线程所有权

- PageControl 是四个分页 HWND 的唯一 owner；白板继续复用 PptBottomLeft/Right。左栏增加标准 2×2 More 后为 305×80 DIP，右栏 230×80 DIP，主体距侧/底各 5 DIP。
- 菜单及主题面板属于左下同一 HWND，向上内联展开。包络、命中、damage 与呈现基于同帧动画几何；左/底锚点不移动，不新建 popup HWND。共享 BarSurfaceScene 行为而非复制按钮状态机。
- More、Escape、面板外点击、翻页、最小化、workspace 转换收起菜单并取消 capture。透明边界穿透，UI 操作不落笔。背景和预览使用同一主题纹理绘制规则。
- 按钮在白板中显示关闭白板，有有效放映时显示返回放映；菜单关闭使用同一退出事务。后台 PPT 结束不得结束白板。
- 主题按页保存，新页继承；默认墨绿 #123B32/空白。色板 #123B32/#FFFFFF/#F5F1E6/#E5E7EB/#25282D/#172A46；空白/点阵/网格，24 DIP 间距、1 DIP 点半径/线宽。主题不改变画笔、不进入墨迹 undo，修改使页 dirty。
- UI 只发布带身份请求并消费快照，Draw3 绘制线程独占页内容/历史；I/O worker 不访问 UI、HWND 或可变 runtime。

## 存储、历史与内存

- 每进程一个逻辑多页 UInk 白板，独立 AutoSave/whiteboard 命名空间；忽略桌面保存开关。同进程关闭重开保留，新进程新建，旧文件保留供未来历史浏览，不自动恢复。
- 保存只在离页/离开白板/正常退出安全点触发。主题、新空页、Undo/Redo/Clear 都是 mutation；无变化不写盘，无定时保存。退出扫描 active 和 parked dirty 白板。
- 流式扫描目录、按页 decode、流式复制未修改页面并原子提交新版本；读取、自校验、hash 及保存均不得全量物化多页墨迹。文件版本与页偏移必须绑定；先 durable UInk 后原子索引。
- 版本化 extra 保存页主题及卸载前可用的完整 CPU 撤销/重做信息，标准 content 保持可见画面语义。GPU 纹理/缓存不得序列化。不扩大既有跨 Clear 规则。
- 非当前页仅在保存成功且 identity/generation/revision 仍匹配时卸载笔迹、历史及CPU/GPU缓存。正常稳态只保留当前页、轻量目录和有限在途数据；同页待保存快照合并。
- 保存失败可继续翻页，失败页 warm 并显示部分页面未保存；后续安全点重试。stale completion 不得释放新修改。冷加载锁定绘制/破坏命令，成功成对切主题/内容/页码；失败保留原页而非创建空白覆盖。

## 场景与底栏

- 首次 Desktop→PPT/Whiteboard 记录桌面主栏快照。只调整已有底栏贴屏底 5 DIP，保持是否居中；浮动、折叠不强制展开。PPT↔Whiteboard 不重拍基线，不重复折叠/复位；最终回 Desktop 恢复基线。
- 白板主屏，PPT 放映屏；同屏连续，跨屏适配显示快照/DPI，原显示器消失按既有回退。
- 最小化是窗口组操作，不是 workspace 退出；恢复不重放入口。保持 NOTOPMOST、单一任务栏锚点及退出穿透/Z序合同。
- 平移缩放仍禁用；页面管理、历史 UI、手动导出、跨进程恢复不在初版内。

## 验证要求

- 生产 Scene 离屏像素/命中/包络测试，覆盖六色三纹理、动画反向/DPI/设备重建/透明区域；不能仅以公式测试代替。
- 真实存储与Controller测试覆盖多页 roundtrip、Undo/Redo/Clear 分支、主题空页、foreign session、写/索引/读失败、快速重入与stale completion。
- 100 非空页保存遍历后非当前页内容/历史/CPU/GPU释放；统计流式buffer峰值，不用失败warm页假冒正常回收通过。
- 完整 InkeysRepo.sln Debug|ARM64 与相关无窗口测试。GUI验收必须另获明确授权；编译/headless不能代替真实PPT/任务栏/视觉验收。
