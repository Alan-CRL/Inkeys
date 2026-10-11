> 2026-10-11 运行时修复直接授权：先阅读 texture-crash-revision.md。真实 DX11 纹理创建失败/部分资源清理及 DeviceLost 传播优先；保留第五轮已批准入场行为。

> 2026-10-11 第五轮已批准：先阅读 brand-entry-revision.md。并行书写后完整停留300ms，再背景渐退与曲线路径归位；仅Show播放、Hide复位主页顶部及实际尺寸门禁覆盖旧冲突约定。

> 2026-10-11 第四轮直接授权：先阅读 soft-sheen-revision.md；克制背景/文案反光、字标不参与及本轮commit边界覆盖旧冲突约定。

> 2026-10-11 第三轮直接授权实施：先阅读 ripple-revision.md；圆头接缝与明显横幅波纹要求覆盖下方冲突约定。

> 2026-10-10 第二轮已批准实施：先阅读 follow-up-plan.md。本轮布局、删除入口及动效要求覆盖下方第一轮的冲突约定；其余要求继续适用。

# 主页品牌横幅、版权信息与开源名片设计

## 目标与授权
用户2026-10-10明确批准已审阅计划实施，新建此子任务挂设置设计父任务；当前工作区/分支最小修改，不GUI/Computer Use，不commit/push。顺序：标题/欢迎语 → 品牌横幅 → Copyright → 开源名片 → 现有探索工具及其余区域。探索/常用设置/作者联系方式/反馈/教程/版本保持。

## 横幅与文案
保留蓝色8 DIP圆角矩形，删除INKEYS眉题、旧标题/说明/右侧示意插画。网站NewHome3完整静态彩虹手写Inkeys居中、标语在下、原书写调整/教程入口居中再往下。保留倾斜/连笔/圆笔触/y下伸，浅蓝+轻微薄荷青/淡紫，深色深蓝灰+网站较亮彩虹。
欢迎语：SC「欢迎使用 智绘教Inkeys 3」，TC「歡迎使用 Inkeys3」，EN「Welcome to Inkeys3」。
标语：SC「让屏幕上的书写行云流水，让每一次操作都赏心悦目。」；TC「讓螢幕上的書寫行雲流水，讓每一次操作都賞心悅目。」；EN「Fluid ink. Beautiful motion. Screen annotation that feels as good as it looks.」。
中文逗号处分两行；英文前两句在上，完整说明在下，自然换行不缩小字体。

## Copyright完整文案
SC:
Copyright © 2023-2026 AlanCRL(陈润林) 工作室
本程序遵循 GNU General Public License v3.0 开源协议。
任何人都可以查看、修改和分发此代码。
TC:
Copyright © 2023-2026 AlanCRL(陳潤林) 工作室
本程式遵循 GNU General Public License v3.0 開源授權條款。
任何人都可以檢視、修改及散布此程式碼。
EN:
Copyright © 2023-2026 AlanCRL (Chen Runlin / Chen Yunlam) Studio
This program is licensed under the GNU General Public License v3.0.
Anyone can view, modify, and distribute this code.
全宽低视觉重量居中真实文本、自然换行。完整许可证名可点击 https://www.gnu.org/licenses/gpl-3.0.html。

## 开源名片
标题SC「开源且免费，为你而造」、TC「開源且免費，為你而造」、EN「Open source and free, made for you」。
说明SC「Inkeys 的代码托管于 GitHub 与 GitCode。」、TC「Inkeys 的程式碼託管於 GitHub 與 GitCode。」、EN「Inkeys code is hosted on GitHub and GitCode.」。
GitHub/GitCode分别可点击 https://github.com/Alan-CRL/Inkeys 与 https://gitcode.com/alan16356/Inkeys。
右侧等宽纵排：星形+普通按钮「为仓库点亮 Star」/「為儲存庫點亮 Star」/「Star on GitHub」，爱心+强调按钮「赞助Inkeys」/「贊助Inkeys」/「Sponsor Inkeys」；Star开GitHub，赞助进已有Support页。参考Lively Fluent视觉、用现有ImFluent。普通主题卡片细边框8DIP圆角；左文本至少280DIP、gap24，不足则按钮移下自然排列/换行。

## 验收
- [x] 三语言全文/分段/署名正确，入口与阅读顺序符合要求。
- [x] 完整字标、浅深/窄宽/多倍率无裁切重叠横向溢出，自动高度和外层滚动正确。
- [x] license/仓库/Star/Support动作正确；轻点一次、拖动取消，绘制不执行外链。
- [x] 既有探索/常用设置/作者和功能保留，滚动控件回归通过。
- [x] 完整Debug|ARM64 Solution、--no-window、i18n、编码/换行、diff检查通过。
- [x] 独立Trellis审查完成；原生HWND/DX11最终观感留人工，不将CPU验证当实机验收。
