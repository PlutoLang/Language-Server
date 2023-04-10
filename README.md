# Pluto Language Server

## Using it with VS Code

Download the .vsix file from [Releases](https://github.com/PlutoLang/pluto-language-server/releases) and drag it into VS Code's Extensions panel.

The server & plutoc executables are bundled and the server is automatically started. If that's too Windows-only or magical for you, [version 0.4.0](https://github.com/PlutoLang/pluto-language-server/releases/tag/0.4.0) of the VSIX still works — you just need to manually set up the language server (see below).

## Using it with other editors

### Set up the language server

1. Grab the latest pluto-language-server.exe from [Releases](https://github.com/PlutoLang/pluto-language-server/releases) or [compile it yourself](#building-the-language-server).
2. Get plutoc either from [Pluto's latest release](https://github.com/PlutoLang/Pluto/releases) or by [compiling Pluto yourself](https://plutolang.github.io/docs/Getting%20Started/#compile-pluto-yourself).

Now you just need to run the server.

*Note that the plutoc executable has to be in the server's working directory for it to properly function.*

**To run the server without a console window**, you can use the [pluto-language-server (no console).vbs](https://raw.githubusercontent.com/PlutoLang/pluto-language-server/senpai/server/pluto-language-server%20(no%20console).vbs) script.

**To run the server at startup**, place a shortcut to either the server executable or "no console" script in `%appdata%\Microsoft\Windows\Start Menu\Programs\Startup`.

### Connect to the language server

Once you've got the server up & running, you'll just have to tell your LSP client to connect to localhost at port 9170.

#### Sublime Text

1. If you don't already have the [sublimelsp](https://github.com/sublimelsp/LSP) package installed, open the command palette and run `Package Control: Install Package`, then select `LSP`.
2. Select `Preferences > Package Settings > LSP > Settings` and either replace or merge the righthand configuration with the following:

```JSON
{
    "clients": {
        "pluto-language-server": {
            "enabled": true,
            "selector": "source.lua",
            "tcp_port": 9170,
            "command": ["ping"]
        }
    }
}
```

### Building the language server

This project uses [Sun](https://github.com/calamity-inc/Sun).

The following dependencies should have the same parent folder as this repository:

- [Soup](https://github.com/calamity-inc/Soup)
- [Pluto](https://github.com/PlutoLang/Pluto)

Inside of the server folder, just run `sun` and you should get the server executable.

## Licensing

This project is dedicated to the public domain. However, note that Soup, Lua, and Pluto have more restrictive licensing.
