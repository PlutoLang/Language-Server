# Pluto Language Server

## Building the language server

This project uses [Sun](https://github.com/calamity-inc/Sun).

The following dependencies should have the same parent folder as this repository:

- [Soup](https://github.com/calamity-inc/Soup)
- [Pluto](https://github.com/well-in-that-case/Pluto)

Inside of the server folder, just run `sun` and you should get a server executable.

However, in order for the server to actually analyse your code, it needs the plutoc executable to be in the same working directory. If you don't have one, run `sun plutoc` in the Pluto/src folder.

## Using the language server

In principle you just have to run the server, then tell your LSP client to connect to localhost at port 5007.

### VS Code

This project includes a VS Code Extension. See [Releases](https://github.com/PlutoLang/pluto-language-server/releases) for VSIX files.

### Sublime Text

1. If you don't already have the sublimelsp package installed, open the command palette and run `Package Control: Install Package`, then select `LSP`.

2. Select `Preferences > Package Settings > LSP > Settings` and either replace or merge the righthand configuration with the following:

```JSON
{
    "clients": {
        "pluto-language-server": {
            "enabled": true,
            "selector": "source.lua",
            "tcp_port": 5007,
            "command": ["ping"]
        }
    }
}
```
