# 实施计划

1. 扩展粗细 Shape/Word 枚举、悬停状态和按压状态，并加入单一三档 DPI 换算入口。
2. 将属性栏宽度调整为 `370`，粗细区调整为 `240×70`，同步右侧笔型列、紧凑比例、上下换边和统一动画范围。
3. 初始化三档与箭头按钮，配置主题色、PointLight 参数和文字对象。
4. 重写粗细区渲染：画笔圆头色带、荧光笔方头色带、真实设备 px 圆形、超限数值和箭头。
5. 将四个按钮接入独立悬停、按压缩放、拖出取消和松手命中逻辑；三档调用 `SetPenWidth()`，箭头保持无业务动作。
6. 核对画笔/荧光笔切换、自定义粗细无选中态、选中光影门禁、隐藏控件命中及现有记忆行为。
7. 保持改动文件原编码和 CRLF，执行：
   - `git diff --check`
   - 静态搜索枚举区间、初始化、布局、渲染、脏区、悬停和输入分支
   - 阅读完整 diff 并映射 PRD 验收项
8. 使用 ARM64 host MSBuild 构建完整方案：
   - `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`
   - `InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`
   - 超时不少于 5 分钟
9. 通过检查后保持任务 `in_progress`，不提交、不归档，等待人工验收。
