#include <iostream>
#include "../lua-5.5.0_Win64_vc16_lib/include/lua.hpp"

#pragma comment(lib, "../lua-5.5.0_Win64_vc16_lib/lua55.lib")

int addthree(lua_State* L)
{
	int x = luaL_checkinteger(L, 1);
	lua_pushinteger(L, x + 3);
	return 1;
}

int main()
{
	const char* luaScript = "print('Hello, Lua!')";

	lua_State* L = luaL_newstate(); // 가상머신 포인터
	luaL_openlibs(L);

	lua_register(L, "addthree", addthree);
	luaL_loadfile(L, "dragon.lua");
	lua_pcall(L, 0, 0, 0);
	lua_getglobal(L, "addthree");
	lua_pushinteger(L, 7);
	lua_pcall(L, 1, 1, 0);
	int result = lua_tointeger(L, -1);
	std::cout << "Result: " << result << std::endl;

	lua_close(L);
}
