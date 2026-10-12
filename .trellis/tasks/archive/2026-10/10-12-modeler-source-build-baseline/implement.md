# Implement: 阶段1执行与验收

## Authorization and Context
2026-10-12批准完整阶段1实施；用户随后授权可行且便于维护时改用vcpkg20250512.1#0。原C++20/MT/NDEBUG/ABI/无行为性能退化约束继续生效。父任务算法保持planning。
按implement/check.jsonl注入规范；缺少marker时自行读取manifest/PRD/design。

## Ordered Checklist
- [x] 原方案批准，子任务创建关联/start及上下文准备。
- [x] 旧merge四配置无窗口baseline保存，19case/128412assert，所有replay同SHA。
- [x] 原独立Abseil构建已记录；用户新授权后停止作为产品路线。
- [x] 导入13cc/共享props/filters/三project及去除旧pragma；修复obj更新作用域，业务flags保持。
- [x] 切vcpkg精确版本，三个triplet专用C++20、统一Release包和active include；核验三架构provider/ABI。
- [x] 新demo/test matrix及主solution Debug ARM64/Release matrix Rebuild；旧/新语义精确比较。
- [x] 30轮成对性能；Headless --no-window；链接来源、imports、旧headers/lib hash、encoding/diff。
- [x] trellis-check及必要复验。
- [x] 更新父/子/spec/journal，明确GUI/Win7真机未执行，不commit/push。

## Validation Commands and Evidence
使用vswhere定位ARM64原生MSBuild；同PowerShell读取原Path、Remove-Item Env:PATH、恢复唯一env:Path，并MSBUILDDISABLENODEREUSE=1。超时默认1800s，检查实际退出码、首个有意义错误，日志到ignored Build。
主solution InkeysRepo.sln /t:Rebuild Debug|ARM64；demo/test inkStrokeModelerTest.sln（Win32 solution平台x86）。output按provider/arch/config/project隔离，防旧obj复用。
精确比较唯一预期依赖版本metadata差异并显式报告，所有模型语义字段继续bit exact；30paired benchmark；Headless --no-window；git diff --check --ignore-submodules；三架构COFF/CRT/namespace/hash/link map及x86/x64imports。

## Rollback and Dependencies
旧merge/旧头/旧EXE及报告保留；回退必须整套切回旧merge+旧头，不混链两个Modeler。失败先定位依赖构建、ABI、flags或源码集成，不调算法/容差掩盖。
后续尾段重构仅在本阶段验收通过后实施。环境问题不污染源码，不升级SDK/toolchain；无GUI、commit、push。
