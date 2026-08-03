# UI3 渲染缓存与 SVG 旋转实施计划

## Ordered Checklist

1. 更新 `BarUiSVGClass`：增加 PNG 风格 `angle`，补充缓存尺寸/内容/设备代际所需元数据和失效入口；确认初始化、直接内容替换、资源重建路径均覆盖。
2. 重构 `BarUIRendering::Svg()` 缓存判断：分离基础布局尺寸与动画目标尺寸；实现尺寸质量阈值、动画期间节流、动画结束补建，并保证 content transition 中点只失效一次。
3. 在 `Svg()` 中加入围绕目标矩形中心的 D2D 旋转，保存并恢复原 transform；同步检查 SVG 脏区/可见区域计算不缩小布局区域。
4. 从 `BarUIRendering::Superellipse()` 抽取局部路径生成 helper，增加按宽高、`n`、分段策略和缩放因子命中的单槽缓存；为当前位置创建平移几何并接入填充、边框、clip、bounds、PointLight/遮罩。
5. 扩展 `DiscardDeviceResources()` 和相关错误恢复，确保 SVG、超椭圆路径、平移几何不跨设备代际存活；保留 CPU 可重算输入。
6. 检查所有调用点和 UI3 Bar 内部状态，确保新增角度字段不改变公开 API、Draw2 兼容和现有几何面板交互。
7. 增加或更新可执行的静态/单元验证：缓存命中与失效条件、动画阈值、内容中点、旋转布局不变、超椭圆位置复用和设备重建。

## Validation Commands

- `git diff --check`
- 使用 ARM64 Host MSBuild 构建：
  `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64`
- 按项目现有测试入口执行 UI3/渲染相关测试；若没有可观测 instrumentation，至少进行代码级断言/静态检查并记录手工验证结果。

## Risky Files And Rollback Points

- `Inkeys/Inkeys/UI/Bar/Bar.UI.cppm` / `Bar.UI.cpp`：SVG 状态、缓存和角度生命周期。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` / `Bar.Main.cpp`：SVG/PNG 渲染、超椭圆路径、设备资源和脏区。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderingAttribute.cppm`：SVG/几何权重区域如需旋转包围盒调整。

回滚时优先恢复 `Svg()` 的缓存策略和 transform，再恢复超椭圆 helper；不要撤销不相关的几何面板或 Draw2 改动。

## Pre-Start Checks

- [ ] 重新阅读 `.trellis/spec/native` 中 runtime/rendering、modules/code-style、quality-and-validation 约束。
- [ ] 确认 SVG 旋转是否需要扩张脏区；若扩张，采用 PNG 同一包围盒公式且不改变命中矩形。
- [ ] 确认尺寸动画的质量阈值和节流字段均为内部常量/状态，不新增配置或持久化字段。
- [ ] 确认超椭圆平移几何对 PointLight、diffuse mask 和 `GetBounds()` 使用同一世界坐标。
- [ ] 完成上述规划审批后，才运行 `python ./.trellis/scripts/task.py start` 并进入产品代码实现。
