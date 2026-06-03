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
		
		_player_head = new Gdiplus::Image(L"Resource/steve_head.png");
		_mob_heads[0] = nullptr;
		_mob_heads[1] = new Gdiplus::Image(L"Resource/zombie_head.png");
		_mob_heads[2] = new Gdiplus::Image(L"Resource/skeleton_head.png");
		_mob_heads[3] = new Gdiplus::Image(L"Resource/creeper_head.png");
		_mob_heads[4] = new Gdiplus::Image(L"Resource/enderman_head.png");
		_mob_heads[5] = new Gdiplus::Image(L"Resource/iron_golem_head.png");

		_gold_icon = new Gdiplus::Image(L"Resource/gold_nugget.png");
		for (int i = 0; i < 20; ++i) _item_images[i] = nullptr;
		_item_images[1] = new Gdiplus::Image(L"Resource/rotten_flesh.png");
		_item_images[2] = new Gdiplus::Image(L"Resource/bone.png");
		_item_images[3] = new Gdiplus::Image(L"Resource/gunpowder.png");
		_item_images[4] = new Gdiplus::Image(L"Resource/iron_ingot.png");
		_item_images[5] = new Gdiplus::Image(L"Resource/gold_ingot.png");
		_item_images[6] = new Gdiplus::Image(L"Resource/diamond.png");
		
		_grass_img = new Gdiplus::Image(L"Resource/grass.png");
		_oak_sapling_img = new Gdiplus::Image(L"Resource/oak_sapling.png");
		_spruce_sapling_img = new Gdiplus::Image(L"Resource/spruce_sapling.png");
		_cactus_img = new Gdiplus::Image(L"Resource/cactus.png");
		
		_health_potion_img = new Gdiplus::Image(L"Resource/health_potion.png");
		_blaze_powder_img = new Gdiplus::Image(L"Resource/blaze_powder.png");
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
	Gdiplus::Image* _player_head;
	Gdiplus::Image* _mob_heads[6];

	Gdiplus::Image* _gold_icon;
	Gdiplus::Image* _item_images[20];
	Gdiplus::Image* _grass_img;

	Gdiplus::Image* _oak_sapling_img;
	Gdiplus::Image* _spruce_sapling_img;
	Gdiplus::Image* _cactus_img;

	Gdiplus::Image* _health_potion_img;
	Gdiplus::Image* _blaze_powder_img;
};
