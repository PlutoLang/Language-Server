#include <iostream>

#include <string.hpp>

#include <lualib.h>
#include <lauxlib.h>
#include <ldo.h>
#include <lstate.h>

using namespace soup;

static int lua_panic_handler(lua_State* L)
{
	std::cout << lua_tostring(L, -1) << std::endl;
	return 0;
}

int main(int argc, const char** argv)
{
	std::string contents = string::fromFile(argv[1]);

	lua_State* L = luaL_newstate();
	lua_atpanic(L, &lua_panic_handler);
	luaL_loadstring(L, contents.c_str());
	lua_close(L);
}
