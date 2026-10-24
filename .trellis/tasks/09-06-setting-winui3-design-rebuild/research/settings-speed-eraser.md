# 笔速橡皮详情页：本轮已批准实施范围（2026-10-24）

用户已明确要求实施上一轮完整计划，并追加暂停图片加入：本轮不生成/接入演示图，不使用效果不佳的附件；下一轮仅生成透明底笔迹，背景与真实擦除由离屏绘制处理。当前先显示介绍文字卡，不放空白图片框或假演示。

## 需求与验收

- 绘制→橡皮擦只保留整行“笔速橡皮”入口，说明“根据绘制意图智能调整橡皮擦粗细”。独立内部子页顶部只有返回箭头，无页面大标题/文字返回链接；绘制侧栏选中，返回恢复原页滚动，复用现有内容过渡。
- 顺序：介绍卡→条件性棕黄 i 提醒→基本设置→精细控制。介绍“结合设备尺寸、输入设备和运动信息，智能推断擦除意图，动态调整橡皮大小。”
- 基本：启用笔速橡皮；UI3 同一配置低/中/高灵敏度；常驻展开启用设备卡，标题条+内联复选区域，共外框/分隔线，五入口鼠标左/右、触摸、笔尖、笔尾；宽双列、窄单列。
- 勾选映射 Speed，取消 Fixed；关闭总开关仍可预设设备/灵敏度，不清空偏好。只在总开关开启且对应笔尖/笔尾启用时显示原计算方式，Win7枚举映射不变。面积辅助总是可见可改，旧配置不变。
- 尺寸提醒按当前绘制 Host 的可信物理尺寸/实际有效标定，不看设置窗口屏幕或原始EDID解析标志；触摸和屏幕笔无物理标尺、DPI退化时显示，即使总开关关闭；有效手动标定不误报，鼠标/数位板正常DIP不是触发原因。
- 提醒标题：无法获取可靠的屏幕尺寸；正文：当前绘制屏幕缺少可信的物理尺寸，触摸和屏幕笔将使用 DPI 估算，橡皮大小可能与擦除意图不符。
- 简繁英完整本地化；不改算法、持久化schema、主栏齿轮、配置/线程生命周期；原setter/队列/每帧单份snapshot。

## 实现设计

设置 Pages 定义纯 draft/events 与内部 DrawingSubpage；Setting.cpp 原生层读取快照并应用事件。共用 Controls 原语实现常驻分组。无新纹理/资源，待下一轮图片满意再接入。只读尺寸提醒复用现有 SpeedEraser ResolveConfig，以当前绘制 display 作为已映射直接输入探针判断触摸/屏幕笔物理可用性，避免复制标定和旋转校验规则；不改变解析器算法。

## 实施与验证

1. 页面/控件/i18n 与原生接线；只读 fallback 判断与定向无窗口边界测试。
2. 真实生产 renderer 无窗口交互/布局覆盖五设备、门控、灵敏度、Win7选择、面积辅助、浅深主题/多语言/窄宽/DPI；导航/scroll 复用路径核对。
3. 独立 trellis-check，ARM64 原生 MSBuild 构建既有 InkeysRepo.sln Debug|ARM64；只用相关 --no-window 测试，按 AGENTS 规范 PATH 和低并发。不 GUI/Computer Use。
4. i18n check、字节格式和 git diff 复核；记录结果与边界。父任务保持 in_progress，不 commit/push，不清理或纳入原有两个 cso 变动。

## 本轮交付与验证结果

- 绘制页橡皮擦分类仅剩长条入口；详情页仅返回箭头、正文介绍卡、条件性 Warning Notice、基本设置与精细控制。图片按用户最新指示暂停，没有添加附件或占位框，也没有改资源表/PNG。
- 原生层每帧一次偏好快照；总开关/灵敏度合并调用已有 global setter，五入口/面积按编辑事件合并写盘。灵敏度与 UI3 使用同一字段/接口；关闭总开关不清除设备偏好，笔计算下拉按条件显隐，面积辅助常驻。
- 永久展开复选组使用现有 Card surface 单描边和分隔线，根据语言实际标签宽决定双/单列；新文案及原总开关名称简繁英同步，i18n check 两语言均514/514 PASS。
- 只读尺寸判断不更改 ResolveConfig 算法，通过当前绘制 Host 已发布的同代快照探测直接输入，既覆盖 DIP 回退，也遵守有效手动标定/旋转/跨屏与间接输入边界。
- 独立 trellis-check 未发现未解决问题，见 settings-speed-eraser-review.md；原生接线、写盘、导航和资源限制均复核。
- ARM64 原生 MSBuild，既有 InkeysRepo.sln Debug|ARM64，全 Solution 最终退出0。第一次源码编译完成后，主程序增量链接器内部 LNK1000 / IncrBuildImage / C0000005，属于本机工具缓存问题；保留 Inkeys.ilk 为 Build/ARM64/Debug/Inkeys.settings-speed-eraser-stale.ilk 后按原配置完整链接成功。未改源码/项目/依赖或工具链规避，保留原第三方 warning。日志为 settings-speed-eraser-build.log 与 settings-speed-eraser-build-final.log。
- --no-window --setting-eraser-only 退出0：实际页面 renderer、7 DPI×3语言×2主题×2宽度=84布局，交互/门控/Win7枚举/返回与独立滚动；尺寸提醒34例；既有笔速和216组橡皮属性布局全部0失败。日志 settings-speed-eraser-tests.log，末尾 settings=0 speed=0 attributes=0。
- git diff --check、UTF-8/BOM/CRLF检查通过；预有两张CSO、PptCOM DLL/TLB、CI workflow、原二维码/GitHub PNG字节不变；CI signal1 按 Git checkout EOL 规范化后与 HEAD 完全一致，未再注入测试数据。产物SHA见 settings-speed-eraser-verification.json。

本轮不运行GUI、外链或Computer Use。无窗口导航夹具复用生产helper/child，但实际原生 Setting.cpp 接线仅静态审查；不宣称Win7实机、硬件或窗口观感已验收。图片后续按透明笔迹+离屏背景/擦除方案继续，本轮未commit/push，父任务保持in_progress。
