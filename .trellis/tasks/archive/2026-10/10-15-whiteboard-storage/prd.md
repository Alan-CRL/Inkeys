# 白板存储与页级恢复

# Whiteboard 初版已批准合同
用户已明确批准本方案并要求实施，无需再次请求建任务或实施许可。禁止 commit/push，禁止可见 GUI/Computer Use。保持最小必要 diff、原编码与 CRLF；关键逻辑中文注释。
关联历史任务：08-17-ui3-whiteboard、08-23-shared-bottom-page-control-windows。保留历史记录。

## 产品要求
恢复白板开关与主栏A2按钮，符合UI3共享按钮规范；活动白板按钮为关闭白板，有可返回PPT时为返回放映，后台放映结束同步更新。菜单关闭与主栏共用退出请求。PPT底栏存在时复用HWND/按钮连续形变，无底栏时白板最终布局渐显；反向中断从当前值续接。
左下下一页右增加标准2x2汉堡更多按钮，基础305x80 DIP，右栏230x80 DIP，离底/侧5 DIP。菜单同左下HWND向上展开：关闭白板、最小化白板、主题样式。主题内联展开，窗口按动画内容包络变化，左底锚点不动。再次更多、Escape、外部点击、翻页、最小化、workspace切换收起并撤销capture；透明区域不挡输入、菜单点击不落笔。
每页独立主题，新页继承当前页。默认#123B32+空白；六色#123B32/#FFFFFF/#F5F1E6/#E5E7EB/#25282D/#172A46。实色带框矩形色块；圆角预览按钮选择空白/点阵/网格。24 DIP间距、点半径1 DIP、线宽1 DIP，深浅背景低透明黑白纹理。预览和背景共用绘制规则，主题不改画笔，不入墨迹undo，但使页dirty。文案进现有i18n，SVG复用资源规范。
整块白板一个逻辑多页UInk10，不是每页文件。AutoSave/whiteboard独立命名空间、进程session隔离；同进程恢复原页，新进程新白板，旧文件保留供未来历史。忽略桌面保存开关。仅离页、离开白板、正常退出保存，无定时保存；退出覆盖parked dirty页；无修改零写，新空页/主题是修改。
支持流式页目录扫描、按页解码、流式复制未改页替换目标页，不全量加载全部页。绘制线程持有runtime，worker处理immutable snapshot。请求含session/document/page/generation/revision；原子新文件再索引，偏移绑定文件版本。主题与版本化完整历史存在应用extra；标准content只表达当前可见墨迹。
翻页不能损失卸载前可用的Undo/Redo，包括redo分支、Clear资格与CPU continuation；不额外扩大原跨Clear规则，不保存GPU。成功且revision匹配才卸载非当前页stroke/history/CPU/GPU，只留目录。队列合并过期同页快照，稳态内存与当前页和有限在途数据相关，不随已保存页墨迹增长。
保存失败仍允许翻页，失败页warm，显示部分页面未保存，在后续离页/关闭/退出重试。快速返回优先最新warm页，stale completion不能覆盖/释放新状态。冷加载锁绘制和破坏命令，就绪后主题/墨迹/页码同步；失败保留原页，不用空页覆盖。退出接入既有屏障和失败处理。
桌面首次进入PPT/白板记录主栏位置、方向、fold、dock/center与显示器。只把已有底栏贴屏底5 DIP，保留是否居中，浮动/折叠保持。PPT/白板互切不重拍桌面快照，不重复折叠/复位；最终回桌面恢复进入前状态。白板主屏/PPT放映屏，同屏连续，跨屏适配DPI；显示器消失按既有回退。最小化非退出，恢复不重放入口。保持NOTOPMOST、任务栏锚点、退出穿透/Z序。
不支持平移缩放、页面管理、手动导出、历史浏览、跨进程恢复。只修本次范围内白板问题，不无关重构。

## 验收
真实共享Scene离屏绘制/像素/命中、菜单包络/DPI/倍率/动画反向/透明边界/设备重建。桌面-PPT-白板状态矩阵和后台PPT结束、不同dock形态及显示器回退。UInk多页roundtrip，主题空页，Undo/Redo/Clear分支，foreign session，桌面保存off；写/索引/读失败、stale completion、快速重入。至少100个非空页，等待保存后检查非当前页CPU/GPU释放及流式buffer界限；失败warm另计。
VS动态定位ARM64原生MSBuild，同PowerShell先Remove-Item Env:PATH -ErrorAction SilentlyContinue及MSBUILDDISABLENODEREUSE=1，构建InkeysRepo.sln Debug ARM64至少5分钟；相关UInk/Draw3与Headless --no-window。不运行GUI；未验证视觉/任务栏/真实PPT明确记录。

## 本任务归属
拥有Draw3目录（含Controller/Host/Bridge/InkHistory）、共享uink codec/file/import/export及相关存储测试；新增共享WhiteboardModel.h提供Theme/Pattern纯值类型。不得修改UI、IdtState、i18n或工程文件；新增编译项交主会话登记。先发布接口给其他代理。
