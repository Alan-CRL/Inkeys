# 追加调整：顶部/侧边任务栏主栏 5DIP（2026-10-19）

用户追加授权取代原规划的顶部/左右贴屏边规则。本轮只调整 Display.cpp 的 ResolveDesktopBottomDockLine 及既有 display_tests.cpp，未触配置、IdtMain、Bar/Startup/PPT 代码、spec 或任务状态。

## 实现
顶部、左侧、右侧任务栏统一返回 monitor.bottom-round(5*effectiveDpiY/96)，DPI 缺失仍按原有 96 回退。与底部自动隐藏 Hidden 分支复用同一局部 screenInsetDockLine 计算，关键注释明确真实屏幕 DIP 与 UI 倍率分工。
底部普通/自动隐藏 Raised 仍返回稳定任务栏 top，Unknown/无任务栏的有效 workArea/屏底回退未改。PPT/Whiteboard 的 scene 优先级在上层 Bar 保留；此次没有改场景判断或 scene 5DIP 计算。

## 生产 helper 回归增量
既有 display_tests 直接调用 ResolveDesktopBottomDockLine，固定 DPI/像素表为 96→5、120→6、144→8、192→10，避免用相同计算公式生成期望值。覆盖顶部/左/右 × 自动隐藏开/关 × 上述四 DPI × 两个含负屏幕原点的矩形（含 bottom 为负），并保留其他 appbar 的 workArea 缺口证明目标以屏幕底部计算。底部 Hidden、Raised 和普通任务栏在四 DPI 上分别验证既有间隙/稳定 top；原 Unknown/无任务栏回退断言保留。

## 验证与交接
两个既有源码文件仍为 UTF-8 BOM + CRLF，git diff --check 通过。修改前 SHA256：Display.cpp=b084513b0f0f6e267adda0494d8e61d4d5e68b064497ff6454fc84bbf010d0aa；display_tests.cpp=3b388624de65ec01eb9fec8fd8080e9cb9ce9791200e85a82cbdd5eacc7e7b37。先前任务的所有未提交改动保留。
按主会话分工，本代理不构建、不运行测试 EXE、不 GUI、不 commit/push；本轮尚不能引用此前完整构建/Headless 结果，主会话待配置默认值增量及 check 稳定后统一执行新产物验证。

静态增量复核：Display.cpp 相对此次追加调整前为 +5/-2 行，display_tests.cpp 为 +29/-10 行。将这两个局部修改在内存中反向还原后，SHA256 与修改前记录完全一致，证明两文件其余既有未提交内容逐字节保留；没有落盘回滚或改写其他文件。
