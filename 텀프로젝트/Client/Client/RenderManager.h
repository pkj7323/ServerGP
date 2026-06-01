#pragma once

#include "Singleton.h"

#include <gdiplus.h>

class RenderManager : public Singleton<RenderManager>
{
	friend class Singleton<RenderManager>;
public:
	RenderManager() : _hBoardBmp(NULL) {
		_helmets[0] = nullptr;
		_helmets[1] = new Gdiplus::Image(L"Resource/copper_helmet.png");
		_helmets[2] = new Gdiplus::Image(L"Resource/iron_helmet.png");
		_helmets[3] = new Gdiplus::Image(L"Resource/diamond_helmet.png");
		_helmets[4] = new Gdiplus::Image(L"Resource/netherite_helmet.png");

		_swords[0] = nullptr;
		_swords[1] = new Gdiplus::Image(L"Resource/wooden_sword.png");
		_swords[2] = new Gdiplus::Image(L"Resource/golden_sword.png");
		_swords[3] = new Gdiplus::Image(L"Resource/copper_sword.png");
		_swords[4] = new Gdiplus::Image(L"Resource/iron_sword.png");
		_swords[5] = new Gdiplus::Image(L"Resource/diamond_sword.png");
		_swords[6] = new Gdiplus::Image(L"Resource/netherite_sword.png");
	}
	~RenderManager();
	void Release() override;
	void Render(HWND hWnd);
	
	void set_board_bitmap(HBITMAP hBmp) { _hBoardBmp = hBmp; }
	HBITMAP board_bitmap() const { return _hBoardBmp; }

private:
	HBITMAP _hBoardBmp;
	Gdiplus::Image* _helmets[5];
	Gdiplus::Image* _swords[7];
};
