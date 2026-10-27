# 2026-10-26 顶边回归修复及 a7dc3a08 复核

## 用户反馈和本轮边界

- 用户已人工确认竖向吸附的脏区外异常消失，可视为该漏清问题修复；脏区内跳变仍可能存在，不能宣称全部闪烁解决。
- 本轮实现顶边上移回归修复，仔细复核 a7dc3a08 的产品变更与错误验收约束，删除可证明多余的代码。用户明确不需要自动测试，等待其人工验证。
- 本轮未编译、未执行算法/离屏/实机测试，也未启动 GUI。下面的公式和场景为静态审查，不是新增运行结果。

## 回归原因与实际修改

旧映射以固定抓点 P 和底端 B 解 H=(B-P)/(1-r)、T=P-rH。B 恢复时 T 必然反向上移，且 r 接近1时需要专用高度弹簧和像素容差。这组代码与测试共同锁定了错误设计。

当前改为明确的顶边目标：Down 从同一成功 snapshot 的屏幕 visualTop 初始化；之后只累计有效输入差，并与 mode/phase/session/translation 同一 seqlock 发布。复用原 pending double 槽，不新增成功输入标量。渲染持有时直接使用目标 top，Docked 底端独立恢复；Floating 存高度偏移。正高度保护只推开底端，不反推顶边。

首张捕获、脱离、新 session、显示环境及 Up/取消的候选均按 targetTop−successfulTop 搬运完整成功形状，不提前积分。失败帧仍从同一成功形状重算。原 presented GrabAnchorValid 槽改为成功 dragActive，准确识别首次松手；交接时速度清零，避免采用失败候选留下的速度。

Floating 的非零 topOffset 同时供刚性 overlay、dirty、命中和光源消费，避免扩展栏停留在旧 root 空间。保持原有 root/directTranslation 吸收公式，不重复移动。

## 上一 commit 的逐项取舍

| 内容 | 处理 | 原因 |
| --- | --- | --- |
| 跨线程 grabBaseRatio、每帧 q0/MapY(grab) 反推输入 | 删除 | 形变后实际抓点不再拥有顶边；顶边目标本身已足够 |
| 1/(1-r)、bottomGrab、1px边界容差与专用高度回写 | 删除 | 都是同时钉住抓点/底端产生的奇点补偿 |
| held 差分速度及其 previousDirectOffset 写入 | 删除手动分支部分 | Up/模式/显示交接从成功形状0速播种，旧计算没有消费者；非手动 fallback 所用字段保留 |
| 额外 actual-top/bottom 最大包络 | 退回原两项保守包络 | Dock偏移为s/b，Float底端为s+h；max(limit,abs(s))+abs(shape)已覆盖，正高度约束也回写shape |
| frame top-valid 重复初始化 | 删除 | 映射解析后统一记录实际有效状态 |
| Down 未呈现 translation 回滚 | 补齐X/Y比较 | 原判断只比较mode/phase/elastic/display，可能在下一手势带入未上屏普通位移 |
| Floating native Move 基准 | 改为成功顶边基准 | 原 raw root 中心与变形后图像不一致，可把首个1px输入放大为额外位移 |
| Down逆映射/实际按钮成功高度/session | 保留 | 用于真实命中与阈值、跨显示环境及同模式再次Down，不能为省原子读删掉 |
| 锁内全局 barrier/serial 复核 | 保留 | 本地awaited=0不能替代其他写者发布的全局屏障 |
| source/target包含、viewport/capacity、成功提交/命中与几何锁 | 保留 | 属于既有呈现一致性合同，不是抓点补偿 |
| 已确认的empty-dirty晚full删除 | 保留 | 用户人工验证脏区外异常已消失 |
| 临时 LiveDockTrace 接线与头文件 | 删除 | 诊断目的已完成；保留TestResults及任务证据，不保留热路径探针与同步CSV导出 |
| 旧全程grab==pointer/底边抓取恢复baseHeight断言 | 修正/删除 | 替换为独立Down top偏移与输入增量、成功端点交接；不再自证错误模型 |

## 直移基准的静态边界复核

Floating本地基准 Bf=成功screenTop−成功directTranslation.y；每次目标Dy=targetTop−Bf，所以原有旧图像只随尚未显示的顶边差直移。形变中再次Down的例子：raw center140、可见center150、Down150、再输入+1时，旧公式会得Dy=11；新基准只增加1。

DPI/显示环境确认时以同一成功tuple重建Bf，不重设顶边目标；普通ack不重建该基准。Docked的窗口目标仍由原吸附tracker决定，模式差继续通过完整呈现barrier配对。Down若pending X/Y translation不等成功值，则沿既有回滚分支恢复完整成功tuple。

手动分支不增加跨线程状态、锁、分配或窗口API。删除了一个每输入写/每帧读的比例原子载荷、抓点除法/边界分支和无消费者的速度计算；没有实测性能数据，不宣称帧率提升。

## 静态检查和剩余验收

- 两位独立产品审查者复核成功快照、DPI、再次Down、native fast path、Up/取消、失败重试与overlay坐标；已处理native基准和pending translation两项阻断意见，末次未发现新的产品阻断项。
- 相关helper全部调用点已同步；旧比例槽、旧抓点标量、bottomGrab恢复特例和临时trace不再有产品引用。
- git diff --check（忽略无关子模块）通过；产品/测试保持原UTF-8编码、BOM和CRLF。默认Git子模块检查受本机worktree访问权限影响，未修改子模块或Git配置。
- 未执行构建/自动测试/GUI；旧PASS不继承到本改动。两份测试只同步纠正错误合同，没有运行。
- 待用户人工确认：静止进入吸附时top保持、bottom独立恢复；形变未结束再拖与松手/取消后再按下无额外跳变；Floating扩展栏绘制/命中/光影跟随。
- 脏区内剩余跳变整体状态仍保留为待人工判断，不以本次静态修复结论冒充其已解决。未commit/push/archive。

最终差异记录：相对 a7dc3a08，四个产品文件净减少81行；两份既有测试净减少45行。局部git diff --check退出0，六个源码文件UTF-8/BOM/CRLF检查正常，旧比例/抓点标量/trace/BottomGrabRecovery产品引用均为0。这是静态文件检查，未运行应用或自动测试。

本轮后续授权：用户要求将当前改动提交commit，成功后允许一次竖直拖动肉眼验证，之后结束本轮。提交前不把未来构建或目视结果写成PASS；实际运行记录保存于TestResults/ui3-vertical-manual-2026-10-26。
