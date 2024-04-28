#include "IdtSysNotifications.h"

#include "wintoastlib/wintoastlib.h"
using namespace WinToastLib;

class IdtSysNotificationsToastHandler : public IWinToastHandler
{
public:
	void toastActivated() const
	{
	}
	void toastActivated(int actionIndex) const
	{
	}
	void toastDismissed(WinToastDismissalReason state) const
	{
	}
	void toastFailed() const
	{
	}
};

bool IdtSysNotificationsImageAndText04(std::wstring AppName, int HideTime, std::wstring ImagePath, std::wstring FirstLine, std::wstring SecondLine, std::wstring ThirdLine)
{
	if (!WinToast::isCompatible()) return false;

	WinToast::instance()->setAppName(AppName);
	WinToast::instance()->setAppUserModelId(AppName);
	WinToastTemplate toast(WinToastTemplate::ImageAndText04);

	toast.setImagePath(ImagePath);
	toast.setTextField(FirstLine, WinToastTemplate::FirstLine);
	if (SecondLine != L"") toast.setTextField(SecondLine, WinToastTemplate::SecondLine);
	if (ThirdLine != L"") toast.setTextField(ThirdLine, WinToastTemplate::ThirdLine);

	toast.setExpiration(false);

	if (WinToast::instance()->initialize())
	{
		INT64 id = WinToast::instance()->showToast(toast, new IdtSysNotificationsToastHandler());

		if (HideTime == 0) return true;
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(HideTime));
			WinToast::instance()->hideToast(id);
		}
	}
	else return false;

	return true;

	return true;
}
bool IdtSysNotificationsText04(std::wstring AppName, int HideTime, std::wstring FirstLine, std::wstring SecondLine, std::wstring ThirdLine)
{
	if (!WinToast::isCompatible()) return false;

	WinToast::instance()->setAppName(AppName);
	WinToast::instance()->setAppUserModelId(AppName);
	WinToastTemplate toast(WinToastTemplate::Text04);

	toast.setTextField(FirstLine, WinToastTemplate::FirstLine);
	if (SecondLine != L"") toast.setTextField(SecondLine, WinToastTemplate::SecondLine);
	if (ThirdLine != L"") toast.setTextField(ThirdLine, WinToastTemplate::ThirdLine);

	toast.setExpiration(false);

	if (WinToast::instance()->initialize())
	{
		INT64 id = WinToast::instance()->showToast(toast, new IdtSysNotificationsToastHandler());

		if (HideTime == 0) return true;
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(HideTime));
			WinToast::instance()->hideToast(id);
		}
	}
	else return false;

	return true;
}