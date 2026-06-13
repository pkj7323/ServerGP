#pragma once

#include "Singleton.h"

#include <gdiplus.h>

class RenderManager : public Singleton<RenderManager>
{
	friend class Singleton<RenderManager>;
public:
	RenderManager() : _hBoardBmp(NULL) {
		_helmets[0] = nullptr;
		_helmets[1] = (HBITMAP)LoadImage(NULL, L"Resource/copper_helmet.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_helmets[2] = (HBITMAP)LoadImage(NULL, L"Resource/iron_helmet.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_helmets[3] = (HBITMAP)LoadImage(NULL, L"Resource/diamond_helmet.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_helmets[4] = (HBITMAP)LoadImage(NULL, L"Resource/netherite_helmet.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

		_chestplates[0] = nullptr;
		_chestplates[1] = (HBITMAP)LoadImage(NULL, L"Resource/copper_chestplate.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_chestplates[2] = (HBITMAP)LoadImage(NULL, L"Resource/iron_chestplate.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_chestplates[3] = (HBITMAP)LoadImage(NULL, L"Resource/diamond_chestplate.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_chestplates[4] = (HBITMAP)LoadImage(NULL, L"Resource/netherite_chestplate.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

		_leggings[0] = nullptr;
		_leggings[1] = (HBITMAP)LoadImage(NULL, L"Resource/copper_leggings.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_leggings[2] = (HBITMAP)LoadImage(NULL, L"Resource/iron_leggings.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_leggings[3] = (HBITMAP)LoadImage(NULL, L"Resource/diamond_leggings.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_leggings[4] = (HBITMAP)LoadImage(NULL, L"Resource/netherite_leggings.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

		_boots[0] = nullptr;
		_boots[1] = (HBITMAP)LoadImage(NULL, L"Resource/copper_boots.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_boots[2] = (HBITMAP)LoadImage(NULL, L"Resource/iron_boots.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_boots[3] = (HBITMAP)LoadImage(NULL, L"Resource/diamond_boots.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_boots[4] = (HBITMAP)LoadImage(NULL, L"Resource/netherite_boots.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

		_swords[0] = nullptr;
		_swords[1] = new Gdiplus::Image(L"Resource/wooden_sword.png");
		_swords[2] = new Gdiplus::Image(L"Resource/golden_sword.png");
		_swords[3] = new Gdiplus::Image(L"Resource/copper_sword.png");
		_swords[4] = new Gdiplus::Image(L"Resource/iron_sword.png");
		_swords[5] = new Gdiplus::Image(L"Resource/diamond_sword.png");
		_swords[6] = new Gdiplus::Image(L"Resource/netherite_sword.png");
		
		_player_head = (HBITMAP)LoadImage(NULL, L"Resource/steve_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[0] = nullptr;
		_mob_heads[1] = (HBITMAP)LoadImage(NULL, L"Resource/zombie_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[2] = (HBITMAP)LoadImage(NULL, L"Resource/skeleton_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[3] = (HBITMAP)LoadImage(NULL, L"Resource/creeper_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[4] = (HBITMAP)LoadImage(NULL, L"Resource/enderman_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[5] = (HBITMAP)LoadImage(NULL, L"Resource/iron_golem_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[6] = (HBITMAP)LoadImage(NULL, L"Resource/cleric_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[7] = (HBITMAP)LoadImage(NULL, L"Resource/armorer_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[8] = (HBITMAP)LoadImage(NULL, L"Resource/weaponsmith_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[9] = (HBITMAP)LoadImage(NULL, L"Resource/librarian_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_mob_heads[10] = (HBITMAP)LoadImage(NULL, L"Resource/villager_head.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

		_gold_icon = (HBITMAP)LoadImage(NULL, L"Resource/gold_nugget.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		for (int i = 0; i < 20; ++i) _item_images[i] = nullptr;
		_item_images[1] = (HBITMAP)LoadImage(NULL, L"Resource/rotten_flesh.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[2] = (HBITMAP)LoadImage(NULL, L"Resource/bone.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[3] = (HBITMAP)LoadImage(NULL, L"Resource/gunpowder.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[4] = (HBITMAP)LoadImage(NULL, L"Resource/iron_ingot.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[5] = (HBITMAP)LoadImage(NULL, L"Resource/gold_ingot.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[6] = (HBITMAP)LoadImage(NULL, L"Resource/diamond.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[7] = (HBITMAP)LoadImage(NULL, L"Resource/health_potion.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[8] = (HBITMAP)LoadImage(NULL, L"Resource/potion.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[9] = (HBITMAP)LoadImage(NULL, L"Resource/ender_pearl.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[10] = (HBITMAP)LoadImage(NULL, L"Resource/ender_eye.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_item_images[11] = (HBITMAP)LoadImage(NULL, L"Resource/blaze_powder.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		
		_grass_img = (HBITMAP)LoadImage(NULL, L"Resource/grass.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_oak_sapling_img = (HBITMAP)LoadImage(NULL, L"Resource/oak_sapling.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_spruce_sapling_img = (HBITMAP)LoadImage(NULL, L"Resource/spruce_sapling.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_cactus_img = (HBITMAP)LoadImage(NULL, L"Resource/cactus.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		
		_health_potion_img = (HBITMAP)LoadImage(NULL, L"Resource/health_potion.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_blaze_powder_img = (HBITMAP)LoadImage(NULL, L"Resource/blaze_powder.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_ender_pearl_img = (HBITMAP)LoadImage(NULL, L"Resource/ender_pearl.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_shield_img = (HBITMAP)LoadImage(NULL, L"Resource/shield.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_arrow_img = Gdiplus::Image::FromFile(L"Resource/arrow.png");
		_fire_img = Gdiplus::Image::FromFile(L"Resource/fire_0.png");
		_dragon_fireball_img = Gdiplus::Image::FromFile(L"Resource/dragon_fireball.png");
		
		_end_portal_frame_img = (HBITMAP)LoadImage(NULL, L"Resource/end_portal_frame_top.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_end_portal_img = (HBITMAP)LoadImage(NULL, L"Resource/end_portal.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
		_end_stone_img = (HBITMAP)LoadImage(NULL, L"Resource/end_stone.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

		_ender_dragon_full_img = Gdiplus::Image::FromFile(L"Resource/Dragon-Sheet.png");
	}
	~RenderManager();
	void Release() override;
	void Render(HWND hWnd);
	
	void set_board_bitmap(HBITMAP hBmp) { _hBoardBmp = hBmp; }
	HBITMAP board_bitmap() const { return _hBoardBmp; }

	void DrawBmpTransparent(HDC destDC, HDC srcDC, HBITMAP hBmp, int x, int y, int w, int h) {
		if (!hBmp) return;
		HBITMAP oldBmp = (HBITMAP)SelectObject(srcDC, hBmp);
		BITMAP bmp;
		GetObject(hBmp, sizeof(BITMAP), &bmp);
		TransparentBlt(destDC, x, y, w, h, srcDC, 0, 0, bmp.bmWidth, bmp.bmHeight, RGB(255, 0, 255));
		SelectObject(srcDC, oldBmp);
	}

private:
	HBITMAP _hBoardBmp;
	HBITMAP _helmets[5];
	HBITMAP _chestplates[5];
	HBITMAP _leggings[5];
	HBITMAP _boots[5];
	Gdiplus::Image* _swords[7];
	HBITMAP _player_head;
	HBITMAP _mob_heads[11];

	HBITMAP _gold_icon;
	HBITMAP _item_images[20];
	HBITMAP _grass_img;

	HBITMAP _oak_sapling_img;
	HBITMAP _spruce_sapling_img;
	HBITMAP _cactus_img;

	HBITMAP _health_potion_img;
	HBITMAP _blaze_powder_img;
	HBITMAP _ender_pearl_img;
	HBITMAP _shield_img;
	Gdiplus::Image* _arrow_img;
	Gdiplus::Image* _fire_img;
	Gdiplus::Image* _dragon_fireball_img;
	HBITMAP _end_portal_frame_img;
	HBITMAP _end_portal_img;
	HBITMAP _end_stone_img;

	Gdiplus::Image* _ender_dragon_walk_img;
	Gdiplus::Image* _ender_dragon_full_img;
};
