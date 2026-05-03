{
  description = "wm-xcb X11 window manager";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            # Build tools
            bear
            clang
            clang-tools
            gcc
            gnumake
            pkg-config

            # XCB libraries
            libxcb
            libxcb-util
            libxcb-keysyms
            libxcb-wm
            libxcb-errors

            # X11 libraries
            libx11
            libxrandr
            libxinerama

            # Freetype
            freetype
            fontconfig

            # Development tools
            gdb
          ];

          shellHook = ''
            echo "wm-xcb development environment loaded"
          '';
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "wm-xcb";
          version = "0.0.1";

          src = ./.;

          buildInputs = with pkgs; [
            libxcb
            libxcb-util
            libxcb-keysyms
            libxcb-wm
            libxcb-errors
            libx11
            libxrandr
            libxinerama
            freetype
            fontconfig
          ];

          nativeBuildInputs = with pkgs; [
            pkg-config
            gnumake
          ];

          configurePhase = ''
            # cd to source root so includes like "src/components/..." work
            cd $sourceRoot
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp wm $out/bin/
          '';
        };
      }
    );
}