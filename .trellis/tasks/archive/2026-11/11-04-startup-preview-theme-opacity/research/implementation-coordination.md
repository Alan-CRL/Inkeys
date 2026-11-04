# 实施协作边界

用户已经明确批准第四版，不再进行规划许可确认。旧研究中的 pending/未批准文字是历史状态，以当前 PRD/design 和本文件为准。

## 所有权

- render 实现代理：Inkeys/Inkeys/UI/StartupPreview/ 全部实现/纯策略/渲染文件、InkeysHeadlessTests/startup_preview_state_tests.cpp。可在 StartupPreview 目录添加必要 header；不改 Config/Setting/IdtMain。
- config 实现代理：Inkeys/Inkeys/UI/StartupPreview.Effect.h（新增共享效果定义）、Other.Config.cppm/.cpp、IdtMain.cpp、Setting 的必要 files、i18n 源及生成物、配置与设置相关 Headless tests。不要修改 render 代理负责的 StartupPreview 子目录/专项测试。
- 主会话：规划/长期 spec、集成构建与测试、最终检查/收尾。构建集中执行，代理不并行启动 MSBuild。

## 冻结的跨代理接口

新增无设备 header Inkeys/Inkeys/UI/StartupPreview.Effect.h，使用已有 StartupPlacement.h 同类组织方式。由 config 代理创建：

namespace Inkeys::UI {
  enum class StartupPreviewEffect : int { Shimmer=0, Aurora=1, PixelAssembly=2 };
  constexpr StartupPreviewEffect NormalizeStartupPreviewEffect(int value) noexcept; // only 1/2 preserved, otherwise Shimmer
}

StartOptions 由 render 代理新增：
- StartupPreviewEffect effect = StartupPreviewEffect::Shimmer;
- int themeMode = 1;
- int backgroundOpacity = 0;
- int contentOpacity = 100;

Config schema 的 StartupPreview.Effect 为整数默认0。IdtMain ReadMini 增读 Effect、ThemeMode、BackgroundOpacity、ContentOpacity；用 NormalizeStartupPreviewEffect 传入 options.effect，另三项对应传入现有归一规则后的百分比/主题。渲染内部可以转换成纯外观策略，避免重复读取配置。

## 执行约束

双方不独占代码库，不能撤销其他代理或原工作区编辑。保留 BOM/CRLF、最小修改、关键逻辑中文注释。明确产品实现支持 D2D effects；共享 D2D device/线程、GDI/ULW 呈现保持。仅无窗口验证；不提交、不推送、不修改外部 HiEasyX 仓库。

优先复用现有元件和测试入口；必要的新测试覆盖真实生产策略/绘制，避免源码字符串检查代替行为验证。建立接口后双方可并行实施；接口变化须先互相告知与主会话协调。
