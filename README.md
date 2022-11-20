# Pluto Language Server

## Using the language server

- [VS Code](#vs-code)
- [Sublime Text](#sublime-text)
- [Other](#other)

### VS Code

Download the .vsix file from [Releases](https://github.com/PlutoLang/pluto-language-server/releases) and drag it into VS Code's Extensions panel.

### Sublime Text

#### Prerequisites

- server
  - [Prebuilt binaries](https://github.com/PlutoLang/pluto-language-server/releases)
  - [Compile it yourself](#building-the-language-server)
- [plutoc](https://plutolang.github.io/docs/Getting%20Started)

#### Steps

1. Ensure you have the server & plutoc executables in the same folder.
2. Start the server.
3. If you don't already have the [sublimelsp](https://github.com/sublimelsp/LSP) package installed, open the command palette and run `Package Control: Install Package`, then select `LSP`.
4. Select `Preferences > Package Settings > LSP > Settings` and either replace or merge the righthand configuration with the following:

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

### Other

#### Prerequisites

- server
  - [Prebuilt binaries](https://github.com/PlutoLang/pluto-language-server/releases)
  - [Compile it yourself](#building-the-language-server)
- [plutoc](https://plutolang.github.io/docs/Getting%20Started)

#### Steps

1. Ensure you have the server & plutoc executables in the same folder.
2. Start the server.
3. Tell your LSP client to connect to localhost at port 9170.

## Building the language server

This project uses [Sun](https://github.com/calamity-inc/Sun).

The following dependencies should have the same parent folder as this repository:

- [Soup](https://github.com/calamity-inc/Soup)
- [Pluto](https://github.com/PlutoLang/Pluto)

Inside of the server folder, just run `sun` and you should get the server executable.

## Licensing

This project is dedicated to the public domain. However, note that Soup, Lua, and Pluto have more restrictive licensing.
