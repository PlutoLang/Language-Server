# Pluto Language Server

## Building the language server

This project uses [Sun](https://github.com/calamity-inc/Sun).

The following dependencies should have the same parent folder as this repository:

- [Soup](https://github.com/calamity-inc/Soup)
- [Pluto](https://github.com/well-in-that-case/Pluto)

### plutolint

You need to modify Pluto by replacing `luaD_throw` in ldo.cpp with the following function:

```C++
void luaD_throw (lua_State *L, int errcode) {
  lua_unlock(L);
  global_State *g = G(L);
  g->panic(L);
  exit(0);
}
```

Now, inside of the plutolint folder, just run `sun` and you should get the plutolint executable.

### server

Copy the plutolint executable into the server folder.

Inside of the server folder, just run `sun` and you should get a server executable.

## Using the language server

In principle you just have to run the server, then tell your LSP client to connect to localhost at port 5007.

This project also includes a VS Code Extension. See Releases for VSIX files.
