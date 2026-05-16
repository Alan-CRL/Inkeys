#pragma once
#include "pch.h"

namespace DirectInkPresenter
{
	namespace Utils
	{
		using namespace Microsoft::WRL;
		using namespace Microsoft::WRL::Wrappers;
		using namespace std;

		static inline void ThrowIfFailed(HRESULT hr)
		{
			if (FAILED(hr))
			{
				throw hr;
			}
		}

		struct AutoCoInitialize
		{
			AutoCoInitialize(DWORD dwCoInit = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)
			{
				ThrowIfFailed(CoInitializeEx(nullptr, dwCoInit));
			}
			virtual ~AutoCoInitialize()
			{
				CoUninitialize();
			}
		};

		template <typename... Args>
		class ComImpletement : public Args...
		{
		public:
			ComImpletement() = default;
			virtual ~ComImpletement() = default;
			IFACEMETHODIMP QueryInterface(REFIID riid, PVOID* ppv)
			{
				static const QITAB qit[] =
				{
					QITABENT(ComImpletement, IUnknown),
					{0},
				};
				return QISearch(this, qit, riid, ppv);
			}
			IFACEMETHODIMP_(ULONG) AddRef()
			{
				return InterlockedIncrement(&m_cRef);
			}
			IFACEMETHODIMP_(ULONG) Release()
			{
				ULONG ref = InterlockedDecrement(&m_cRef);

				if (ref == 0)
				{
					delete this;
				}

				return ref;
			}
		private:
			ULONG m_cRef = 1;
		};

		template <typename T>
		static inline auto ComAs(IUnknown* pUnknown)
		{
			ComPtr<T> instance{ nullptr };
			ThrowIfFailed(pUnknown->QueryInterface(IID_PPV_ARGS(&instance)));
			return instance;
		}
		template <typename T, typename Ptr>
		static inline auto ComAs(const ComPtr<Ptr>& pUnknown)
		{
			ComPtr<T> instance{ nullptr };
			ThrowIfFailed(pUnknown.As(&instance));
			return instance;
		}
	}
}