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
	lua_getglobal(L, "addtwo");
	int pos_x = lua_getglobal(L, "pos_x");
	int pos_y = lua_getglobal(L, "pos_y");
	err = lua_pcall(L, 2, 1, 0);
	if (err != LUA_OK)
	{
		std::cerr << "Error: " << lua_tostring(L, -1) << std::endl;
	}
	int result = lua_tointeger(L, -1);
	std::cout << "Result: " << result << std::endl;


	lua_close(L);
}
