# 技术设计

拥有UI/Whiteboard、UI/PageControl、Bar.SurfaceScene相关共享扩展与相关UI测试、UI SVG/i18n白板菜单文案；不要修改Bar.Main/Bar.Button/BottomDock/IdtState/Draw3。先与storage协定Theme接口，UI通过callbacks发布请求，不直接访问runtime。

复用既有Draw3 Host、RenderPipeline、BarSurfaceScene和Window Service，禁止第二套运行时或窗口。所有异步结果按身份与revision门控。UI所有权与业务callbacks分离。详细合同见prd.md。

集成顺序：storage发布纯值/Bridge接口→UI主题及菜单→scene联动→总体检查；接口明确后允许独立文件并行。主会话独占工程文件与spec更新。

## 落地设计

PageControl 保留稳定 widget id，左下第四按钮及菜单、色板、预览共用 BarSurfaceScene。菜单变化通过既有 TransitionLayout 与窗口 bounds 插值呈现；同帧几何供绘制、命中及窗口提交使用。白板菜单打开或动画锁定时，可见面板阻止输入穿透；透明边界保持穿透。

WhiteboardVisual::DrawTheme 同时绘制背景和预览，纹理参数固定为24 DIP/1 DIP。按钮按下时记录显示的page/generation，主题回调携带该身份并在业务层拒绝迟到操作。Host与Whiteboard发布完整页面快照，避免页码与主题串页。

白板 UI、主栏和输入线程通过回调及异步取消消息协作；低级mouse和原始RTS/Pointer down均只请求收起菜单，不同步等待窗口线程。
