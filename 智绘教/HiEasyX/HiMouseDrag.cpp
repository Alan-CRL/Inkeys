#include "HiMouseDrag.h"

namespace HiEasyX
{

	bool MouseDrag::UpdateDragInfo(bool& btn, int msgid_down, int msgid_up)
	{
		if (newmsg)
		{
			if (btn)
			{
				dx = msg.x - old.x;
				dy = msg.y - old.y;
				old = msg;

				if (msg.message == msgid_up)
				{
					btn = false;
				}

				if (dx != 0 || dy != 0)
				{
					return true;
				}
				else
				{
					return false;
				}
			}
			else
			{
				if (msg.message == msgid_down)
				{
					btn = true;
					old = msg;
				}
				return false;
			}
			newmsg = false;
		}
		else
		{
			return false;
		}
	}

	void MouseDrag::UpdateMessage(ExMessage m)
	{
		msg = m;
		newmsg = true;
		ldrag = UpdateDragInfo(lbtn, WM_LBUTTONDOWN, WM_LBUTTONUP);
		mdrag = UpdateDragInfo(mbtn, WM_MBUTTONDOWN, WM_MBUTTONUP);
		rdrag = UpdateDragInfo(rbtn, WM_RBUTTONDOWN, WM_RBUTTONUP);
	}

	bool MouseDrag::IsLeftDrag()
	{
		bool r = ldrag;
		ldrag = false;
		return r;
	}

	bool MouseDrag::IsMiddleDrag()
	{
		bool r = mdrag;
		mdrag = false;
		return r;
	}

	bool MouseDrag::IsRightDrag()
	{
		bool r = rdrag;
		rdrag = false;
		return r;
	}

	int MouseDrag::GetDragX()
	{
		return dx;
	}

	int MouseDrag::GetDragY()
	{
		return dy;
	}

};
