#pragma once

#include "../../additional/HiMsg/HiMsg/HiMsg.hpp"

// 传统 Draw2/PPT 代码仍使用 EasyX 风格名称，实际类型由 HiMsg 唯一定义。
using ExMessage = HiMsg::ExMessage;

inline constexpr BYTE EM_MOUSE = static_cast<BYTE>(HiMsg::MessageFilter::Mouse);
inline constexpr BYTE EM_KEY = static_cast<BYTE>(HiMsg::MessageFilter::Key);
inline constexpr BYTE EM_CHAR = static_cast<BYTE>(HiMsg::MessageFilter::Char);
inline constexpr BYTE EM_WINDOW = static_cast<BYTE>(HiMsg::MessageFilter::Window);
