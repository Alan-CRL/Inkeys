# 实施计划与完成记录

## 门禁与责任
- [x] 用户在最新规划摘要之后明确批准实施，再由task.py start进入in_progress。
- [x] trellis-before-dev加载native-desktop规范；按Trellis auto模式派发实现/检查agent，根会话协调接口、需求和验收。
- [x] 维持单任务分阶段，不并行改同一Setting/ImGui文件。字体agent负责Typography、字体加载及依赖；热切换agent在字体接口稳定后负责i18n/各UI消费者；review agent最后跨层检查。不得回退其他人的修改。
- [x] 不自动commit/push，不启动GUI；保持原编码、BOM、换行和中文关键注释。

## P0：固化基线及消费者清单
- [x] 审计所有生产i18n读取、静态标签、字体族、布局/截图/提交缓存；形成清单含状态型按钮、子面板、独立renderer、自有标题和消息框。
- [x] 保存cf4d9120的实际字体加载和配置基线；复用现有真实字体/DX11离屏夹具，不复制控件替身。
- [x] 增补必要的测试专用内存/时序计量，保持产品行为；明确统计口径、暖态判定和采样环境。
- [x] 记录11语言与倍率基线PNG、glyph/排版度量、字体相关CPU/D3D资源及冷/热耗时，保存task/research或validation文档，生成图/大日志放Build或忽略的runtime目录。

## P1：FreeType依赖与最小对照
- [x] 核对现有baseline解析到的FreeType版本和许可；加入manifest最小features，按现有静态triplet构建，不改其他依赖或工具链。
- [x] 引入官方v1.92.7 imgui_freetype源文件，登记生产与测试工程以及来源记录。
- [x] 建立真实stb/FreeType离屏A/B，采用源密度1与LightHinting，并验证Harmony/Noto Legacy/Design缩放和主脸/merged源基线；Default模式保留为研究备选，未宣称已实测；验证图标处理。
- [x] 选择满足既有视觉的参数，记录原因和对照图。未达到外观或内存目标不能直接进入全量集成。

## P2：生产字体生命周期
- [x] 复用动态PushFont真实字号；减少额外密度和无效全后端重建，保留纹理事务、故障注入、Retry/DeviceLost以及缓存失效。
- [x] 仅维护当前+有界候选资源；同字族复用、跨字族安全替换，按ImGui销毁协议释放旧对象。处理font指针、ImFluent绑定、font serial、旧DrawData/UV。
- [x] 验证既有bake回收，必要时在安全边界最小化补充有界回收。预算仍失败才评估R8/源复用等进一步方案；不得顺手全局改gamma或nearest。
- [x] 复跑外观与内存阶段门，覆盖设置显隐、DPI/用户倍率和epoch恢复。

## P3：全界面热切换
- [x] 准备/发布语言revision，选择UI只排队；候选准备成功且安全提交后才持久化，连续请求最后有效目标收敛。
- [x] Setting字族与11语言重启文案同步；保留页面、编辑、滚动/导航状态，撤销旧字体页面缓存。
- [x] 原位刷新Bar标准/状态型/组件标签与字体、所有展开面板、tooltip、布局/适配字号缓存和场景renderer。
- [x] 刷新PageControl/Whiteboard/PPT、自有标题/提示等消费者，唤醒可见表面并标记隐藏表面版本；保持业务状态。
- [x] 验证已有消息框request语言快照不变，新请求同版本取正文/按钮/字族，失败不落盘错误选择。

## P4：联合回归与性能复核
- [x] 对照设计文档固定工作负载，检查全部A1–A8；11语言至少3轮往返及DPI循环资源收束。
- [x] 注入语言/字体/纹理失败、重复请求、隐藏/显示、缓存重放与epoch恢复；测试真实生产入口，不只测辅助纯函数。
- [x] 同机串行A/B输出暖态median/p95、冷帧/切换最慢样本、内存分类与瞬时峰值；可复现性能退化不能凭主观认为可接受。
- [x] 提交真实PNG对照供用户评估视觉；没有明确GUI授权就仅用无窗口和离屏，标记原生交互尚未验证。

## 验证命令与构建约束
先核对实际CLI及脚本帮助，沿已有入口：

```powershell
# 用vswhere定位当前VS的ARM64原生MSBuild，保持PATH修复与MSBuild在同一invocation。
# 若runner同时提供Path/PATH，先保存有效Path，移除重复Env:PATH后恢复唯一规范变量，保留post-build命令查找。
$env:MSBUILDDISABLENODEREUSE = '1'
& $taskMsbuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m
# 总超时至少5分钟，建议30分钟；进程未退出时持续观察，不因短期无输出重启。
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window --setting-general-only
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window --setting-home-only
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window --setting-font-pages-only
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window --setting-font-benchmark-only
& .\Build\ARM64\Debug\Inkeys.exe --i18n-locales-test
& .\Build\ARM64\Debug\Inkeys.exe --bar-eraser-offscreen-test
pwsh -NoProfile -File Scripts/i18n.ps1 -Command check
git diff --check --ignore-submodules
```

专项在开发迭代按变更选择，最终完整无窗口套件通过后无新修改不重复大范围测试。记录每个命令退出码、首个有意义错误和重要相关warning。环境错误不得靠改源码/SDK绕过。

## 主要风险文件与回退点
- `Setting.Typography.h` / `Setting.cpp`：尺寸、角色、session/atlas与缓存；P0基线保护视觉，P2字体接口稳定后再进入P3。
- `imgui_impl_dx11.cpp` / ImGui shader：项目已有错误处理/纹理测试；仅必要局部改动，不覆盖为上游文件。
- `IdtI18n.cpp/.h` / Bar / PageControl / MessageBox：语言发布、跨线程、缓存/业务状态；增加revision而非广泛重构。
- vcpkg、vcxproj/filter：仅FreeType必要集成，不升级baseline；所有未测试架构明确说明。
- JSONC及生成键：复用脚本，验证占位格式与旧语言索引不变。

## 收尾
- [x] Trellis check agent审核PRD、真实生产路径测试和无关diff。
- [x] 根会话核对自动验证、离屏图和文件格式，更新字体/热切换规范及任务记录；用户已确认效果符合预期，验收后仅移除语言说明信息图标。
- [x] 用户已确认观感并授权commit和归档；收尾后记录实际提交。未授权push。
