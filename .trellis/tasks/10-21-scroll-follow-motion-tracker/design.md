# 可信二维视觉追踪与特征接力设计

## 当前范围

纯图像VisualMotionTracker，Initialize/Process/Reset，不依赖HWND、Draw3、Mag、UInk或主栏；不创建线程。沿现有CV/Headless与OpenCV4.10 core/imgproc/video，不增calib3d/highgui和依赖。当前用户批准全阶段实现；本任务仅离线图像，无GUI或Computer Use，不commit/push。

具体研究见 ../10-21-scroll-follow-mag-capture/research/tracker-design-current.md。该研究是可实施建议，尚未实测数值不是已通过结论。取帧正式接口单独由捕获子任务负责，合成序列不证明真实屏幕产品通过。

## 输入与事务

公开ImageView仅标准库/数值：top-down Gray8/BGRA32、size/stride/width/height、时间，数据借用限当次调用，内部保留自有参考灰度。先做溢出/布局校验再构造cv::Mat。输出accepted、原图像素double delta/total、confidence[0,1]、原因、统计与暂停锁存。

半分辨率灰度按真实处理尺寸比例换回原图，不硬编码偶数乘2。内容向上输出负Y。只在可信帧一次提交参考、存活点、补点和total；拒绝时保持所有可信状态，短拒后与最后可信参考比较，不吞中间位移。完全相同的有效有纹理帧可正常接受零位移，不用deadband或hash近似归零。

## 光流与共识

初始8x6网格约320角点、GFTT、LK21x21/maxLevel3，FB约1处理px；按格归一化投票、确定性纯平移一致集与内点加权中位数。起点有效/内点约40、空间内点比例65%、残差1.5处理px；结合覆盖、有效点存活、FB、残差、捕获质量和跳变形成可信度，硬门优先。固定工具栏角点数多数但面积小不能主导估计；分布不足/多组运动歧义应拒绝。

显著旋转/缩放/重排只作拒绝，不能输出它们的变换。可用core小矩阵诊断位移场，不能只在平移裁剪后的中心点上检测变形。初始阈值可由测试校准并明确记录，但不能放宽误差验收或用GT选择结果。

## 特征接力与误差

保留每点出生内容锚点(pointOriginal-totalAtBirth)，不能每帧重建旧锚点。可用局部亚像素重新定位减小增量偏差，须重新验证FB与同一点一致性。可信帧在稀疏格补点，新点从当前total反算锚点，下一帧才参与估计；不能以补点重置世界原点。

普通低可信连续3帧Paused，严重无效输入/捕获立即Paused，保持最后可信total，不自动恢复。Initialize/Reset显式建立新会话。

## 测试与进入下一阶段

测试直接调用生产Tracker，对不可变非周期长画布按绝对camera坐标取样，GT独立于算法；覆盖静止300帧与微噪声、整数和亚像素、至少十屏长滚动/初始特征全离屏、0.25px小步、水平反向/二维、固定UI角点多数、动态干扰、拒绝后保留参考及三次暂停、黑/纯色/特征不足、旋转缩放重排、布局/stride/奇数尺寸及输入寿命。

静止漂移<=0.5原图px；每个合法prefix累计误差<=max(3px,实际路程*0.2%)，路程为路径长度不是净位移。报告每场景接受/拒绝、max/final/step误差、接力与初始存活、原因及性能，不删失败序列刷过门槛。若严格目标未达，保留失败并继续定位，不擅自降低标准。

定向--visual-motion-tracker-only与既有--no-window，完整solution Debug ARM64，随后trellis-check。源码格式保持，主会话推进下一子任务。
