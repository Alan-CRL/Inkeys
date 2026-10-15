# 技术设计

协调三个子任务，统一验证、更新spec，不直接替代子任务实现。

复用既有Draw3 Host、RenderPipeline、BarSurfaceScene和Window Service，禁止第二套运行时或窗口。所有异步结果按身份与revision门控。UI所有权与业务callbacks分离。详细合同见prd.md。

集成顺序：storage发布纯值/Bridge接口→UI主题及菜单→scene联动→总体检查；接口明确后允许独立文件并行。主会话独占工程文件与spec更新。
