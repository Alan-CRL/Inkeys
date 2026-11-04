# 技术设计

拥有Draw3目录（含Controller/Host/Bridge/InkHistory）、共享uink codec/file/import/export及相关存储测试；新增共享WhiteboardModel.h提供Theme/Pattern纯值类型。不得修改UI、IdtState、i18n或工程文件；新增编译项交主会话登记。先发布接口给其他代理。

复用既有Draw3 Host、RenderPipeline、BarSurfaceScene和Window Service，禁止第二套运行时或窗口。所有异步结果按身份与revision门控。UI所有权与业务callbacks分离。详细合同见prd.md。

集成顺序：storage发布纯值/Bridge接口→UI主题及菜单→scene联动→总体检查；接口明确后允许独立文件并行。主会话独占工程文件与spec更新。

## 落地设计

`DrawingController` 保留白板 document slot、每页 revision/saved/queued/cold/failed、主题目录和当前页历史。热页离开时捕获不可变单页快照；`WhiteboardStore` 串行后台写入，最多保留两个等待保存请求，同页合并。冷页读取带运行 generation；控制器验证回执后恢复历史，使用带同一代次的 SetPage 完成安装，安装前保持输入锁。

一个进程会话一个逻辑文档，在 AutoSave/whiteboard/<session>/ 内写不可变版本与原子 current 索引。共享 uink_page_file 模块扫描轻量目录、按需解码页并用固定块复制未修改页；强 revision 绑定内容哈希、文件身份和偏移。发布索引后保留当前版本及一份备份。

`inkeysWhiteboardHistory` v1 保存全CPU笔迹、可见性、redo顺序、Clear边界和continuation；标准content只导出可见笔迹。保存回执与当前revision相等才允许EvictWhiteboardPage释放非当前页CPU内容，GPU缓存由切页路径释放。失败保留热页，下一个安全点重试；正常退出排空并报告durable结果。
