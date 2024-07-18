/**
 * @file	HiMusicMCI.h
 * @brief	HiEasyX 库的声音模块
 * @author	悠远的苍穹 <2237505658@qq.com>, huidong（修改）
*/

#pragma once

#include <Windows.h>

namespace HiEasyX
{
	/**
	 * @brief <pre>
	 *		MusicMCI（音乐播放操作层）
	 *		支持播放、暂停、设置音量、获取播放时间等操作
	 * </pre>
	*/
	class MusicMCI
	{
	private:
		MCIDEVICEID nDeviceID;									///< 设备ID

	public:
		MusicMCI()noexcept;										///< 默认构造函数
		virtual ~MusicMCI();									///< 虚析构函数
		bool open(LPCTSTR music)noexcept;						///< 打开音乐
		bool play()noexcept;									///< 播放音乐
		bool pause()noexcept;									///< 暂停音乐
		bool stop()noexcept;									///< 停止播放
		bool close()noexcept;									///< 关闭音乐
		bool getCurrentTime(DWORD& pos)noexcept;				///< 获取当前播放时间
		bool getTotalTime(DWORD& time)noexcept;					///< 获取音乐总时长
		bool setVolume(size_t volume)noexcept;					///< 设置音量大小
		bool setStartTime(size_t start)noexcept;				///< 设置播放位置
	};
};
