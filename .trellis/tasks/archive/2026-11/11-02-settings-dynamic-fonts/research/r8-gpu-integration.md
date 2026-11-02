# Setting ImGui Alpha8 CPU / R8 GPU 实现

仅设置ImGui后端，未修改Bar/PageControl的DWrite路径。

- 新增imgui_alpha_ps.hlsl/CSO与资源322（IDR_SETTING_IMGUI_ALPHA_PS），生产和headless工程/RC共同登记。
- Alpha8在设备支持R8纹理采样时原样上传；RGBA32图片保持旧格式。能力不足时继续临时展开Alpha8→白RGB+alpha的RGBA回退。
- 每backend内以已成功创建SRV的纹理链表识别R8；支持缓存DrawData的裸SRV ID，不依赖TexRef._TexData。创建失败不注册，销毁先移除链表再释放。
- 普通绘制按纹理选择RGBA或单通道PS。用户回调后查询实际PS：后端自有PS继续自动选择，无操作/只修改采样器的回调不破坏混合绘制；外部PS保持直到ResetRenderState或回调显式恢复后端PS。
- 新PS返回vertexColor * float4(1,1,1,coverage.r)，线性采样/混合保持原样；没有修改gamma。
- 测试用TestForceRgbaAlpha可创建同Alpha8数据的RGBA回退参照；增加R8/回退真实GPU读回、偏移/分离更新、故障重试、裸SRV像素对照、no-op/采样器回调与不同字节码外部PS测试。

源差异空白检查通过。首次R8完整构建由dynamic_fonts报告通过80.67s；其后回调修正及测试需要重编译。实际像素/资源/性能结果由构建owner统一运行记录，此文档不表示这些验证通过。
