# deadbeef-pxtone
deadbeef-pxtone is a DeaDBeeF decoder plugin for pxtone Collage music files.

## Development
A Nix flake is included, as well as a corresponding `.envrc`. If you have Nix installed with flakes enabled, you can run a copy of DeaDBeeF with this plugin installed using `nix run .`.

To develop using Nix, ensure you have Nix installed with flakes enabled, as well as direnv. Run `direnv allow` to allow the envrc to be applied. From there, you can use `make` to build the plugin.

To develop without using Nix, you must ensure that you have libvorbis, libvorbisfile, libogg, GNU Make, and GCC available in the local environment, then you can use `make` to build the plugin.

## Installing
The `make install` target will install the plugin to a DeaDBeeF installation at `/opt/deadbeef`, which is the location it will be if you are using DeaDBeeF's Debian/Ubuntu package. You can control the DeaDBeeF path by passing `DEADBEEF_ROOT` to the Makefile, e.g. `make DEADBEEF_ROOT=... install`.

There is a NixOS overlay and module in the Nix flake that will provide the plugin under `pkgs.deadbeefPlugins.pxtone`.
