# 白板背景分类 Headless 回归

- 责任文件：InkeysHeadlessTests/whiteboard_ui_tests.cpp。
- 新增 TestBackgroundColorClassification，接入既有 RunWhiteboardUiTests；直接调用生产 IsLightWhiteboardBackground，不复制亮度计算。
- 覆盖现有六色、128灰等阈值归深/129灰归浅、红绿通道顺序；将合法主题的 Blank/Dots/Grid 分别作为样本，分类仅传入 RGB。
- 仅增加生产头文件 include 和测试函数/调用，不修改产品/工程/spec。
- 已执行 git diff --check（目标文件）并查看 diff；保持原 UTF-8 无 BOM、CRLF。
- 按主会话职责划分，未构建、未运行测试、未启动 GUI、未 commit/push；运行验证由主会话执行完整 Solution 后完成。
