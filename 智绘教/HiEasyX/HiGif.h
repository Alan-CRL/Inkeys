/**
 * @file	HiGif.h
 * @brief	HiEasyX 库的动图模块
 * @author	依稀_yixy，huidong（修改）
*/

#pragma once

#include <graphics.h>
#include <gdiplus.h>
#include <time.h>
#include <stdio.h>

namespace HiEasyX
{
	/**
	 * @brief <pre>
	 *		Gif 动图
	 *
	 *	使用方法：
	 *		1. load 加载图像
	 *		2. bind 绑定输出 HDC
	 *		3. （可选）setPos 设置位置
	 *		4. （可选）setSize 设置缩放大小（为 0 表示原图大小）
	 *		5. play 开始播放
	 *		6. draw 绘制到 HDC
	 * </pre>
	 * 
	 * @bug <pre>
	 *		1. 释放时可能崩溃
	 *		2. 对绑定的 HDC 调整大小可能导致崩溃
	 * </pre>
	*/
	class Gif
	{
	private:

		int x, y;
		int width, height;
		int frameCount;					///< 帧数

		HDC hdc;						///< 设备句柄
		Gdiplus::Graphics* graphics;	///< 图形对象

		Gdiplus::Bitmap* gifImage;		///< gif 图像
		Gdiplus::PropertyItem* pItem;	///< 帧延时数据

		int curFrame;					///< 当前帧
		clock_t pauseTime;				///< 暂停时间

		clock_t	frameBaseTime;			///< 帧基准时间
		clock_t	curDelayTime;			///< 当前帧的已播放时间
		clock_t	frameDelayTime;			///< 当前帧的总延时时间

		bool playing;					///< 是否播放
		bool visible;					///< 是否可见

	public:

		Gif(const WCHAR* gifFileName = nullptr, HDC hdc = GetImageHDC());
		Gif(const Gif& gif);

		virtual ~Gif();

		Gif& operator=(const Gif& gif);

		/**
		 * @brief 加载图像
		 * @param[in] gifFileName 图像文件路径
		*/
		void load(const WCHAR* gifFileName);

		/**
		 * @brief 绑定设备
		 * @param[in] hdc 绘图设备
		*/
		void bind(HDC hdc);

		/**
		 * @brief 清空图像
		*/
		void clear();

		// 位置
		void setPos(int x, int y);
		void setSize(int width, int height);

		int getX() const { return x; }
		int getY() const { return y; }

		// 设置后的图像大小（为 0 表示使用原图大小）
		int getWidth() const { return width; }
		int getHeight() const { return height; }

		// 原图大小
		int getOrginWidth() const;
		int getOrginHeight() const;

		// 帧信息
		int getFrameCount() const { return frameCount; }
		int getCurFrame() const { return curFrame; }

		// 延时时间获取，设置
		int getDelayTime(int frame) const;
		void setDelayTime(int frame, long time_ms);
		void setAllDelayTime(long time_ms);

		// 更新时间，计算当前帧
		void updateTime();

		// 绘制当前帧或指定帧
		void draw();
		void draw(int x, int y);
		void drawFrame(int frame);
		void drawFrame(int frame, int x, int y);

		/**
		 * @brief 获取图像
		 * @param[in] pimg		载体
		 * @param[in] frame	帧索引
		*/
		void getimage(IMAGE* pimg, int frame);

		// 播放状态控制
		void play();
		void pause();
		void toggle();

		bool IsPlaying()const { return playing; }

		void setVisible(bool enable) { visible = enable; }
		bool IsVisible() const { return visible; }

		bool IsAnimation() const { return frameCount > 1; }

		/**
		 * @brief 重置播放状态
		*/
		void resetPlayState();

		void info() const;

	private:

		/**
		 * @brief 初始化
		*/
		void init();

		/**
		 * @brief 读取图像信息
		*/
		void read();

		void copy(const Gif& gif);
	};

};
