#include <iostream>
#include "../lua-5.5.0_Win64_vc16_lib/include/lua.hpp"

#pragma comment(lib, "../lua-5.5.0_Win64_vc16_lib/lua55.lib")

int main()
{
	const char* luaScript = "print('Hello, Lua!')";

	lua_State* L = luaL_newstate(); // 가상머신 포인터
	luaL_openlibs(L);

	luaL_loadfile(L, "dragon.lua");
	int err = lua_pcall(L, 0, 0, 0);
	if (err != LUA_OK)
	{
		std::cerr << "Error: " << lua_tostring(L, -1) << std::endl;
	}

	lua_getglobal(L, "pos_x"); // 스택에 pos_x를 올림
	lua_getglobal(L, "pos_y"); // 스택에 pos_y를 올림
	int pos_y = lua_tointeger(L, -1); // 스택에 두개 숫자	가 올라가 있으므로 -1은 pos_y, -2는 pos_x
	lua_pop(L, 1); // 스택에서 pos_x와 pos_y 제거
	int pos_x = lua_tointeger(L, -1); // 스택에 올려진 순서대로 -1, -2로 접근
	lua_pop(L, 1); // 스택에서 pos_x 제거
	std::cout << "pos_x: " << pos_x << ", pos_y: " << pos_y << std::endl;

	lua_close(L);
}
