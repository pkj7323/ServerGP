#pragma once

#include "Singleton.h"

class RenderManager : public Singleton<RenderManager>
{
	friend class Singleton<RenderManager>;
public:
	RenderManager() : _hBoardBmp(NULL) {}
	~RenderManager();

	void Render(HWND hWnd);
	
	void set_board_bitmap(HBITMAP hBmp) { _hBoardBmp = hBmp; }
	HBITMAP board_bitmap() const { return _hBoardBmp; }

private:
	HBITMAP _hBoardBmp;
};
