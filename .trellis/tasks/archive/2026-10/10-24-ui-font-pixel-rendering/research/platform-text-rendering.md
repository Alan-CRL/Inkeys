# Research: DirectWrite字号与像素边界

- Query: 非整数像素字号、DPI、像素对齐与抗锯齿的关系。
- Scope: external
- Date: 2026-10-24

## Findings

- DirectWrite的字号单位是DIP；D2D的默认DIP单位模式下，实际像素为DIP乘DPI/96。PIXELS单位模式可避免这次自动单位缩放。必须调查产品实际context DPI及变换，不能对已手动缩放到px的字号再乘显示器DPI。
- NATURAL渲染模式允许子像素位置；NATURAL_SYMMETRIC还会垂直抗锯齿，通常用于更大字号。非整数像素字号本身不证明渲染错误，整数截断是本任务用户指定的兼容策略，真实观感仍需人工设备验收。
- 灰度抗锯齿可表达透明目标alpha，不能为追求清晰直接在透明layered窗口切到ClearType而忽略合成背景。
- 本次保留既有抗锯齿/后端，策略只作用主栏和控件字号；明确排除设置字体。关闭时最终物理字号向下截断，开启保留原值；量化应在用户倍率/继承倍率/内容动画倍率合并后发生。

## External references

- https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-high-dpi
- https://learn.microsoft.com/en-us/windows/win32/learnwin32/dpi-and-device-independent-pixels
- https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_rendering_mode
- https://learn.microsoft.com/en-us/windows/win32/direct2d/direct2d-and-directwrite

## Caveats / Not Found

尚无本次真实大屏截图或观感对照；不能承诺所有模糊只由小数字号导致。字号截断不等同逐字形坐标强制对齐，也不应将round套到字号。
