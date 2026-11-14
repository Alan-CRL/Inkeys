# 第二次 PowerPoint 实机反馈：关闭期间的集合 getter 同样阻塞

输入：d5639afe-b3a4-4527-a6ba-9e85f71bcf5a/已粘贴的文本.txt，模块 a2f05be4-df20-4231-acf0-62321e5b8f1f，与上一轮候选一致。

- 4037–4038 行已记录 2025/2004 通知。
- 4039 行 qpc=82484074701，开始 get:Presentations。
- 4040 行 qpc=82765495529，返回 0x800706BE，真实 elapsed_ticks=281419972 / 10000000 = 28.1419972 秒。此次可明确排除控制台输出时间。
- 4044 行记录原始 event_qpc=82484118755、DISPID=2031，该通知在 getter 阻塞期间已经到达。
- 后续 ActivePresentation 报 0x800706BA，旧绑定成功进入清理；Unadvise 同样报 0x800706BA 后正确消费 CP。原绑定 service-end 汇总 858/858，末尾空轮询累积为 988/988。没有残余已跟踪取得。

结论：上一轮 RPC 不可用清理已生效，但“关闭时可独立查询 Presentations.Count”的前提被实机证据否定。等待从窗口 getter 转移到了集合 getter；不能再换一个对象模型属性继续尝试，也不以添加 sleep/强制超时/FinalRelease 代替根因处理。

## 新处理边界

- 本机项目使用的 PowerPoint 15 PIA 只读反射确认：BeforeClose=2025、Close=2004、PresentationCloseFinal=2031。
- 官方参考：[PresentationCloseFinal](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.application.presentationclosefinal)。这里把它作为文稿最终关闭通知，不把它误当作整个应用已经没有其他文稿。
- 2025/2004 只暂停一轮，不查询正在关闭的对象模型；未收到最终通知时按原维护机制恢复取消关闭/兼容轮询。
- 收到当前 binding 的 2031 后，owner 直接退休本模块的绑定并沿原 finally 解绑、释放、GC，再按既有服务发现流程重连。不会调用 Office Quit/Close；剩余文稿保持打开，但联动会短暂重新绑定。
- 不在事件回调内释放，不增加事件参数 RCW、原生指针身份包装或新的对象模型查询。0x800706BE 不因本次出现就被全局扩大解释为服务器已死。
- 新增 Unadvise 的纯调用耗时，若清理仍有等待，可继续定位而不混入输出时间。

## 回归

新增四项在上一候选上失败；修改后完整托管 suite 与 47 项原生 COM 夹具通过。新断言在禁止所有对象模型访问的条件下发送 2031，验证服务不靠 stop 信号自行结束、CP 恰次解绑/释放、root 配平。既有取消关闭、其他文稿、busy Count0 和无事件轮询回归保持通过。

日志：validation/final-close-red-build.log、final-close-red-tests.log、final-close-green-build.log、final-close-green-tests.log。

实际 PowerPoint/WPS 的退出延时仍待本轮候选实机验证，自动化通过不等同实机问题已解决。
