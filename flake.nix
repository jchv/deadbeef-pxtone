{
  description = "DeaDBeeF plugin for playing pxtone Collage music";
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        deadbeef-pxtone = pkgs.callPackage ./package.nix { };
        deadbeef-with-pxtone =
          (pkgs.deadbeef-with-plugins.override {
            plugins = [ deadbeef-pxtone ];
          }).overrideAttrs
            {
              meta.mainProgram = "deadbeef";
            };
      in
      {
        packages = {
          inherit deadbeef-pxtone deadbeef-with-pxtone;
          default = deadbeef-with-pxtone;
        };
        checks = {
          inherit deadbeef-pxtone;
          basic = pkgs.callPackage ./tests/basic.nix { inherit self; };
        };
        devShells.default = pkgs.mkShell {
          inputsFrom = [ deadbeef-pxtone ];
        };
      }
    )
    // {
      overlays.default = final: prev: {
        deadbeefPlugins = prev.deadbeefPlugins // {
          pxtone = final.callPackage ./package.nix { };
        };
      };
      nixosModules.deadbeef-pxtone = {
        config = {
          nixpkgs.overlays = [ self.overlays.default ];
        };
      };
    };
}
