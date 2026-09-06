# 设计审查稿 v1

打开 `index.html` 可查看独立效果稿。它从仓库相对路径读取 HarmonyOS Sans、Fluent 图标和应用图标，无 CDN 或外部服务依赖。也可在仓库根运行 `python -m http.server 8765 --bind 127.0.0.1` 后访问：

`http://127.0.0.1:8765/.trellis/tasks/09-06-setting-winui3-design-rebuild/preview/index.html`

## 可以审查的内容

- 主页、完整常规页、语言样张、插件列表和子页返回层级。
- 汉堡展开/收起、窄窗覆盖面板、Esc/遮罩关闭。
- 开关、滑块、下拉选择和置顶间隔的折叠详情。
- 侧栏其余入口只展示页面占位并明确说明，未完成全量页面效果稿。

控件状态仅为演示；不写产品配置，不执行自启、重启、退出、创建快捷方式或外部链接。原生产品页面尚未修改。标题栏只是布局占位，不是本轮窗口样式改造。

## 图片

| 文件 | 尺寸 / 含义 |
| --- | --- |
| 01-general-wide.png | 1200×840 常规页 |
| 02-home-wide.png | 1200×840 主页 |
| 03-general-compact.png | 1200×840 手动收起导航 |
| 04-general-default-960.png | 960×700 默认窗口 |
| 05-general-minimum-720.png | 720×520 最小窗口 |
| 06-navigation-overlay-720.png | 720×520 导航覆盖展开 |
| 07-home-default-960.png | 960×700 主页 |
| 08-general-reflow-360.png | 360 宽内容预算下的重排示意；不是原生 DPI 测试 |

效果图用浏览器栅格化实际内嵌字体，仅用于目标视觉审查。实施后仍需 ImGui/STB 离屏样张验证字面、基线和裁切；不能把这些图片当作产品已实现证据。
