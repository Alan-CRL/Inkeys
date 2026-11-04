# 设置与 i18n 实施记录

- Setting.Pages.h/cpp 增加仅三值草稿/三事件的 RenderPptIdleFadeScopes，以实际 CheckboxGroup 复用内联类型布局。关闭时禁用并保留偏好。
- Setting.cpp 在原闲置下拉框后消费三事件，只给真实变化字段赋值，三个事件合并一次 QueueConfigWrite 和 IdleFade 通知，仍在原 FIFO coroutine 域。
- 新增四个 scope 文案键；全部 20 个 locale 修改准确说明，明确主栏仅按闲置期限、外部翻页仅让分页立即淡化、附属面板展开保护主栏。
- i18n sync 生成头和 zh-CN snapshot；按项目语义去除本次已完成翻译的源文案变更标记，i18n check 实际退出码 0，所有翻译语言 724/724。
- setting_design_tests.cpp 直接调用同一生产函数，通过真实 NavRect、Mouse Down/Up 验证单次事件、默认值、独立编辑、全不选、disabled 不写回和再启用；覆盖全部语言、340/780 DIP、1.0/1.5/1.875 三倍率。root 已在完整构建后执行新产物：UI 专项与完整 --no-window 均退出 0。
- UI 专项环境 INKEYS_SETTING_PPT_IDLE_ONLY=1，沿用 --no-window --setting-home-only；同时纳入完整设置回归，无新 GUI 入口。
- 源文件按修改前 BOM/UTF-8/CRLF 原样保存；task 文档统一 CRLF。没有 commit/push，没有 GUI。
