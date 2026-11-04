module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <limits>
#include <string>

#ifdef MessageBox
#undef MessageBox
#endif

module Inkeys.UI.MessageBox;

namespace Inkeys::UI::MessageBox::Detail
{
	bool IsResultAllowed(Buttons buttons, Result result) noexcept
	{
		switch (buttons)
		{
		case Buttons::Ok:
			return result == Result::Ok;
		case Buttons::OkCancel:
			return result == Result::Ok || result == Result::Cancel;
		case Buttons::YesNo:
			return result == Result::Yes || result == Result::No;
		default:
			return false;
		}
	}

	bool ValidateRequest(const Request& request) noexcept
	{
		if (!request.title || !request.body) return false;
		if (request.requireOwner && !request.owner) return false;
		if (!IsResultAllowed(request.buttons, request.defaultResult)) return false;
		if (request.dismissEnabled)
		{
			if (request.buttons == Buttons::YesNo)
			{
				if (request.dismissResult != Result::Dismissed) return false;
			}
			else if (!IsResultAllowed(request.buttons, request.dismissResult))
			{
				return false;
			}
		}
		if (request.owner && request.ownerlessTopmostAtCreation) return false;
		if (request.reliability == Reliability::CriticalNoWait && request.owner)
			return false;
		if (request.owner)
		{
			DWORD processId = 0;
			if (!IsWindow(request.owner)
				|| !GetWindowThreadProcessId(request.owner, &processId)
				|| processId != GetCurrentProcessId())
			{
				return false;
			}
		}
		return true;
	}

	UINT BuildFallbackFlags(const Request& request) noexcept
	{
		UINT flags = request.fallback.modality == SystemModality::System
			? MB_SYSTEMMODAL : MB_APPLMODAL;
		switch (request.buttons)
		{
		case Buttons::Ok:
			flags |= MB_OK;
			break;
		case Buttons::OkCancel:
			flags |= MB_OKCANCEL;
			break;
		case Buttons::YesNo:
			flags |= MB_YESNO;
			break;
		}
		if (request.fallback.icon == SystemIcon::Error) flags |= MB_ICONERROR;
		if (request.defaultResult == Result::Cancel || request.defaultResult == Result::No)
			flags |= MB_DEFBUTTON2;
		return flags;
	}

	Result MapSystemMessageResult(Buttons buttons, int value) noexcept
	{
		switch (value)
		{
		case IDOK:
			return buttons == Buttons::Ok || buttons == Buttons::OkCancel
				? Result::Ok : Result::Failed;
		case IDCANCEL:
			return buttons == Buttons::OkCancel ? Result::Cancel : Result::Failed;
		case IDYES:
			return buttons == Buttons::YesNo ? Result::Yes : Result::Failed;
		case IDNO:
			return buttons == Buttons::YesNo ? Result::No : Result::Failed;
		default:
			return Result::Failed;
		}
	}

	int ScaleDipValue(int dip, UINT dpi) noexcept
	{
		if (dpi == 0) dpi = 96;
		const auto value = static_cast<long long>(dip) * dpi;
		const auto rounded = value >= 0 ? (value + 48) / 96 : (value - 48) / 96;
		return static_cast<int>(std::clamp<long long>(rounded,
			std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
	}

	ButtonLabelSet ResolveButtonLabels(LANGID language)
	{
		if (PRIMARYLANGID(language) == LANG_CHINESE
			&& (SUBLANGID(language) == SUBLANG_CHINESE_TRADITIONAL
				|| SUBLANGID(language) == SUBLANG_CHINESE_HONGKONG
				|| SUBLANGID(language) == SUBLANG_CHINESE_MACAU))
		{
			return { L"確定", L"取消", L"是", L"否" };
		}
		if (PRIMARYLANGID(language) == LANG_CHINESE)
			return { L"确定", L"取消", L"是", L"否" };
		switch (PRIMARYLANGID(language))
		{
		case LANG_JAPANESE: return { L"OK", L"キャンセル", L"はい", L"いいえ" };
		case LANG_KOREAN: return { L"확인", L"취소", L"예", L"아니요" };
		case LANG_FRENCH: return { L"OK", L"Annuler", L"Oui", L"Non" };
		case LANG_GERMAN: return { L"OK", L"Abbrechen", L"Ja", L"Nein" };
		case LANG_SPANISH: return { L"Aceptar", L"Cancelar", L"Sí", L"No" };
		case LANG_PORTUGUESE: return { L"OK", L"Cancelar", L"Sim", L"Não" };
		case LANG_RUSSIAN: return { L"ОК", L"Отмена", L"Да", L"Нет" };
		case LANG_INDONESIAN: return { L"OK", L"Batal", L"Ya", L"Tidak" };
		case LANG_VIETNAMESE: return { L"OK", L"Hủy", L"Có", L"Không" };
		case LANG_TURKISH: return { L"Tamam", L"İptal", L"Evet", L"Hayır" };
		case LANG_ITALIAN: return { L"OK", L"Annulla", L"Sì", L"No" };
		case LANG_POLISH: return { L"OK", L"Anuluj", L"Tak", L"Nie" };
		case LANG_UKRAINIAN: return { L"Гаразд", L"Скасувати", L"Так", L"Ні" };
		case LANG_ROMANIAN: return { L"OK", L"Anulare", L"Da", L"Nu" };
		default: return { L"OK", L"Cancel", L"Yes", L"No" };
		}
	}

	int BuildButtonSpecs(Buttons buttons, const ButtonLabelSet& labels,
		ButtonSpec (&output)[2])
	{
		output[0] = {};
		output[1] = {};
		switch (buttons)
		{
		case Buttons::Ok:
			output[0].result = Result::Ok;
			output[0].label = labels.ok;
			return 1;
		case Buttons::OkCancel:
			output[0].result = Result::Ok;
			output[0].label = labels.ok;
			output[1].result = Result::Cancel;
			output[1].label = labels.cancel;
			return 2;
		case Buttons::YesNo:
			output[0].result = Result::Yes;
			output[0].label = labels.yes;
			output[1].result = Result::No;
			output[1].label = labels.no;
			return 2;
		default:
			return 0;
		}
	}
}
