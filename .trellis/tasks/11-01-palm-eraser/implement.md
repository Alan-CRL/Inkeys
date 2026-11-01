# 手掌橡皮实施计划

状态：用户已批准，代码实现与命令行验收已完成，详见[验证记录](research/validation.md)；未做GUI/实机，未commit/push。完整合同见[prd.md](prd.md)和[design.md](design.md)。真实时间交错研究已停止，不作为实施前置或上下文。

## 1. 开始前的检查

- [x] 在后续用户消息明确批准最新最终规划之后，才运行task.py start .trellis/tasks/11-01-palm-eraser。
- [x] 通过trellis-before-dev重新加载任务三份文档、native-desktop索引及本次实际触及的输入/配置/橡皮/显示/设置合同，必要时读native的运行/几何部分。索引注入不能代替读取长spec的相关全文。
- [x] 确认当前checkout与用户已有修改，记录将编辑文件的编码/BOM/EOL，不创建新worktree或commit。
- [x] 验证implement/check上下文有真实spec与当前研究条目，不包含已停止的两份temporal研究。
- [x] 固定变更边界：只实现手掌识别、触点覆盖/接续、并行门禁、详情页与配置；不修改时间Z序、UInk格式、公共历史架构、普通笔速曲线、旧Draw2/demo绘制镜像。

## 2. 预计必要文件

| 文件/范围 | 修改理由 |
| --- | --- |
| 新Draw3.PalmEraser.h/.cpp | 无Win32 UI依赖的设置值、工具分类、标尺/面积/尺寸解析、状态机及邻域策略，生产与测试同源 |
| Draw3.DrawingController.cpp/.cppm | Down/Move/原地更新/截止/Up、候选转换事务、仅观察接触、邻域与擦写门禁、真实断触接续继承及无窗口生产probe |
| Draw3.Host.h/.cpp | 配置快照/owner应用、幂等唤醒，沿用已应用显示标尺；不添加第二个runtime或输入producer |
| Inkeys/Other/Other.Config.cppm/.cpp | Drawing.PalmEraser字段、冻结大屏缺省、严格JSON验证与复制/重置 |
| IdtState.cpp、IdtMain.cpp | 复用配置/业务同步和启动发布，早期无GUI的配置与Controller验证入口；只改实际必要接线 |
| Setting.Pages.h/.cpp、Setting.cpp | 草稿/事件、PalmEraser子页与独立child ID、同款入口/Warning/Combo/滑块重置/七项勾选/实尺预览 |
| Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc | 新页和控件三语文案；150ms推荐后缀与刷子正常可勾选 |
| IdtI18nKeys.g.h、Scripts/i18n.zh-CN.snapshot.jsonc | 仅经既有i18n sync生成 |
| InkeysHeadlessTests/palm_eraser_tests.cpp（新）、setting_design_tests.cpp、animation_tests.cpp及必要既有配置probe | 有意义的策略边界、真实控件回放、注册与配置验证；不测试实现细节的镜像 |
| Inkeys/Inkeys.vcxproj、InkeysHeadlessTests/InkeysHeadlessTests.vcxproj及必要filters | 仅登记新增源码/测试，不改SDK、依赖、平台矩阵或构建入口 |

以上Draw3.*位于Inkeys/Inkeys/Drawing/Draw3，Setting.*位于Inkeys/Inkeys/UI/Setting。既有Fixed模式支持逐新增点读取baseDiameter，原则上不需改InkPrediction/StrokeGeometry；若实际需要一个局部共用helper，先说明必要性，不扩成新宽度体系。Controls通用封装不为本页做无关重构，重置按钮优先本页局部组合。

## 3. 执行顺序

### A. 纯策略与配置

- [x] 建立七种工具偏好；Brush现在可编辑保存，未实现工具选择；Shape映射所有现有几何工具，不按DrawingTool枚举下标误映射。
- [x] 可信25cm²、失效回退100×100系统DIP基准，面积倍率25..400%、窗口100/150/250/500ms默认150；设置/运行期单位、非法值及每Down锁存规则一致。
- [x] 可信物理/现有有效标定与10000系统DIP²回退解析（EDID失败不禁用，缺省大屏仍开启）；不共享笔速面积辅助的跨度上限、运动资格或尺寸上限。
- [x] 有限/正值/CanvasPixels/比例≤3.5检查、有效最大面积、等面积圆直径与三种预览尺寸公式。
- [x] 配置NoUpload、缺省分类、缺失/非法类型/越界/超宽整数、ReadMini/ReadAll/复制/Reset及真实roundtrip。
- [x] Host启动、运行设置、重置和重启发布同一偏好快照，重复值不唤醒。

### B. 真实触点转换与生命周期

- [x] 在位移/状态未变早退之前消费面积，在帧循环处理窗口截止；采用既有合并快照口径，不新增原始输入队列。
- [x] 新Down按已有准入锁存状态，合格即激活；临时Fixed直径只影响后续新增几何，窗口后稳定到逻辑终态。
- [x] 用准备后提交的方式替换本触点模型/几何；保留selectedTool/global工具/其他Runtime，失败保留原可收尾内容。
- [x] 清理本触点的L0/L1原工具CPU几何、Shape与Laser自有层，重建共享层并覆盖旧脏区，不清L2/文档。
- [x] 原实际工具已是橡皮时保留已接受擦除点，只切换后续尺寸，不恢复已擦内容；其他原工具从激活位置建立橡皮，不把旧绘制路径变成擦除。
- [x] 原地面积变化的圆环/实际擦除尺寸一致；无Hover；不使用SpeedEraserOcController增长或普通eraser尺寸覆盖。
- [x] Up/Cancel/页边界/失效generation/pool复用正确清理，不复活已撤去的旧原工具墨迹。

### C. 断触接续与并行门禁（先验证再接完整页面）

- [x] 在新触点分类前尝试既有断触接续；针对旧手掌候选构造一致的有效工具/Fixed身份，不放宽时间、方向、位置等真实匹配门槛。
- [x] 原绘制成功续接仍为绘制，不因新Down大面积升级；原手掌成功续接保留相同尺寸，设置修改也不覆盖旧状态。
- [x] 窗口前Up即冻结已接受尺寸/关闭识别，成功接续不重开窗口；失败/超时回独立新接触。普通Laser/Shape续接范围不扩展，已转手掌者按Touch Eraser处理。
- [x] 只在模型Update成功后转交handle，失败保留旧候选，最终正常续接只产生一个history动作。
- [x] 相邻抑制锁存至接触终态，含同批仲裁、待续接邻域预约及非相邻多手掌；旧已激活手掌靠近不反向取消。
- [x] 关闭边擦边写时正常收尾其他绘制、保留真实墨迹并隔离route；新Touch可仅识别合格远处手掌，不产生新绘制，其他输入拒收到终态。
- [x] 全局多指关闭仍拒收第二个独立Touch；真正成功接续沿既有优先规则。激光多指保持原全局/子项合取。
- [x] 等待截止/Move/终态沿原唤醒协议；不为新功能改变普通Fixed/Speed的无关等待或帧率行为。

### D. 详情页与资源

- [x] 绘制→擦除分类的笔速入口下方添加手掌入口，独立子页/返回/滚动恢复/正文过渡，不改根侧栏。
- [x] 草稿/编辑事件与原生Host/配置写盘分离；总开关、Warning、窗口Combo、倍率与同条目重置、预览、七工具、精细开关按design顺序。
- [x] 默认开关复用启动冻结大屏分类，保存值不随用户切交互模式改变；总开关关时仍允许编辑偏好。
- [x] 预览按像素直绘，1:1/2:1/1:2等面积；窄窗换行，单例超宽可横向查看，不缩放伪装实尺；DPI回退/目标屏幕/未发布状态文案准确。
- [x] Brush可正常勾选，默认勾上；不显示“暂不可用”禁用态，不新增刷子工具按钮。
- [x] 三语资源经Scripts/i18n.ps1 sync/check生成与核验。

## 4. 验证方式与通过标准

### 不创建窗口的验证

1. 策略测试覆盖面积/比例等号、窗口前/到点/之后、原地摊掌、非法单位/NaN/无穷/零/负、最大值不回缩、提前Up冻结、DPI/物理换算、10000DIP²在100%/150%/200%系统缩放的像素面积、七工具与邻域仲裁。
2. 真实生产helper probe覆盖同handle转换与失败回滚、本触点旧几何消失而其他Runtime/L2保留、Shape/Laser、仅观察接触、并行开/关、严格单Touch。
3. 接续覆盖普通绘制→大面积新Down仍绘制、手掌→不同面积新Down尺寸完全一致、150ms前Up、设置/显示变化、模型接续失败、未匹配/超时新接触、多个候选不串状态及单次逻辑Undo。
4. 沿既有文档/history/保存探针验证普通整笔Eraser、两种完成先后、Undo/Redo/新分支、重建与roundtrip；不比较已取消的真实时序覆盖结果。
5. 真实ImGui/字体/DrawData回放三语×浅深×窄宽×多个DPI，验证导航、150ms推荐、七项含Brush编辑、百分比重置只改倍率、状态保存、预览矩形像素尺寸不随UI缩放。

需要新增生产无窗口入口时使用IdtMain既有早期自测路由，建议名--draw3-palm-eraser-test，调用与Run共用的真实helper并可用离屏WARP资源；不要用--draw3-hidden-test或--draw3-eraser-hidden-test，它们会创建HWND，不在本期授权内。配置测试私有scratch目录，必要时仅为测试进程将TEMP/TMP设为仓库可写目录，不改产品写盘器绕过环境。

### 构建与命令

- 先确认主构建系统仍为InkeysRepo.sln/MSBuild；不单独构建Inkeys.vcxproj，不创建其他构建系统。
- 用vswhere或当前VS安装发现原生ARM64 MSBuild，路径不硬编码VS版本。
- 同一个PowerShell invocation先Remove-Item Env:PATH -ErrorAction SilentlyContinue并设置MSBUILDDISABLENODEREUSE=1，再执行：MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /nodeReuse:false。完整构建至少5分钟，建议10分钟预算；持续会话等待结果，不因短时无输出重复启动。
- 使用Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window --setting-eraser-only及新增生产无窗口probe；相关专项通过后执行一次InkeysHeadlessTests.exe --no-window作跨模块回归，不无故重复全部测试。
- 执行Scripts/i18n.ps1 check；最后git diff --check与status/diff范围、编码/BOM/EOL核对。受子模块git metadata沙箱权限影响时，用--ignore-submodules=all检查本任务范围，不修改子模块配置。
- 检查真实exit code与首个有意义错误；区分源码/链接与SDK、PATH、权限、SSH/runner环境，不升级依赖或改代码绕过本机问题。

## 5. 风险点、回滚与结束条件

- 重点审查Controller的转换、接续、强制收尾及pool清理；同一handle只能有一个活动owner。临时手掌不得污染batchTool/global工具记忆。
- 识别使用消费快照近似峰值，缺少面积元数据的设备无法激活；DPI估算不承诺厘米准确，真实手感仍需设备验证。
- 出现超出design的公共协议/Shader/真实时间Z序需求时先回到规划，不自行扩展；已停止研究不重启。
- 先保持可独立关闭的Palm旁路与普通路径，再接UI。验证发现转换失败不能保留原内容时，修正事务而非取消其他人的输入掩盖问题。
- 代码完成后应用trellis-check做本任务全范围检查，并按trellis-update-spec只补实际确认的新合同；不提前把假设写成spec事实。
- 无用户明确授权不启动主窗口、设置窗口、隐藏HWND、浏览器或Computer Use。没有执行的硬件/GUI验证在最终交付明确列出。
- 不创建git commit、不push。验证通过后再报告实际变更/测试结果；当前规划文档不等同功能实现。
