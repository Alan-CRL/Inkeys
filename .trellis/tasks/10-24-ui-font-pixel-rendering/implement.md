# 执行计划

## 授权与工作方式
用户已在任务初始消息批准规划和实现，后续明确排除设置文字。按Trellis auto模式由implement/check子代理执行产品修改与审查；主会话负责规划、完整构建、验证记录和spec收尾。禁止commit/push和GUI测试。

## 顺序
- [x] 新建任务、加载Trellis上下文、确认用户范围。
- [x] 调查字体最终变换、配置与实验页、三语资源、官方DirectWrite单位与抗锯齿边界。
- [x] 编写统一字号解析并接入全部主栏/控件实际文字入口，保留现有布局测量上界。
- [x] 新增默认false严格bool配置、实验卡、三语和生成物；轻请求后在各render owner中产生damage和正确缓存失效。
- [x] 在既有Headless/配置/离屏测试中加入必要回归，保持最小工程改动。
- [x] i18n sync/check、diff格式和编码换行检查；trellis-check审查并自修明确局部问题。
- [x] ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，合理超时不少于5分钟，实际预算15分钟。同一PowerShell invocation先Remove-Item Env:PATH及MSBUILDDISABLENODEREUSE=1。
- [x] 新产物InkeysHeadlessTests.exe --no-window；需要时使用已确认不创建HWND的字体离屏入口。测试TEMP/TMP使用本任务仓库scratch，避免已知Codex临时目录原子替换权限故障。
- [x] 更新相关code-spec和research/validation.md，检查最终diff，保留未提交修改供用户审阅。

## 回归断言
- 字号：48DIP基准(0.6)、96/120/144/192DPI、任意zoom、内容/按压/弹出/非均匀缩放；向下截断而非round；整数保持、非整数不放大、0至1px、非法输入与allow=true。
- 生产绘制/离屏：Word及直接文字解析实际字号，缓存/策略双向切换和隐藏恢复；保持图标和布局既有连续缩放。
- 配置：默认/缺失/非法类型false、true/false真实读写、Mini隔离、复制/冻结/重置、NoUpload。
- 设置：准确中文标题、三语无占位翻译、父容器有足够高度，不改变ImGui文字渲染。

## 风险检查
特别检查Word之后的外层D2D变换、独立细刻度/帧率格式、只Request但无damage、隐藏分页和失败帧重试。不用重构或修改工具链绕过环境失败。

## 动画连续性修订（首轮提交之后）
用户最新要求已并入PRD/design；此前完成记录属于首轮，当前不自动commit/push、不结束任务。
- [x] 明确只限制静止字号和动画目标，沿用现有任务。
- [x] 调查原始字号/动画事务和外层稳定目标，补充两份research并配置上下文。
- [x] 在动画内部实现有界的连续字号修正目标，原始字号与几何动画保持既有语义。
- [x] 各主栏/控件/独立文字路径传递静止目标，接入共享帧推进、active和damage；保留设置文字。
- [x] 更新三语说明和无窗口回归，验证中间帧、结束帧及重定向连续性。
- [x] Trellis审查、完整Debug|ARM64 solution、Headless --no-window及字体离屏验证。
- [x] 更新code-spec和本轮validation记录，最终diff/编码换行检查；保留任务进行中。
