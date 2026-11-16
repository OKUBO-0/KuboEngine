# Vendored Lua

Official Lua 5.4.9 archive: https://www.lua.org/ftp/lua-5.4.9.tar.gz

Verified SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`

The upstream `src/` and `doc/` files are unmodified. Kubo adds only this note,
`LICENSE.txt` (the notice from `src/lua.h`) and `KuboLua.vcxproj`.
The library is statically linked, excluding the interpreter/compiler CLI entry
points. Distributions include `ThirdPartyLicenses/Lua-5.4.9.txt`.
