#include "pch.h"
#include "resource.h"
#include "Window.h"
#include "Canvas.h"
#include "Application.h"
#include "Graphics.h"

void DirectInkPresenter::Application::Initialize(HINSTANCE hInstance)
{
	UI::Graphics::Initialize();
	UI::Window::Initialize(hInstance);
	UI::Window::Create<UI::Canvas>();
}

int DirectInkPresenter::Application::Run()
{
	return UI::Window::Run();
}
