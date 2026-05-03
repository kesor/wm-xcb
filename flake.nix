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
            bear
            clang
            clang-tools
            gcc
            gnumake
            pkg-config
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
            libxcb libxcb-util libxcb-keysyms libxcb-wm libxcb-errors
            libx11 libxrandr libxinerama freetype fontconfig
          ];
          propagatedBuildInputs = with pkgs; [ xvfb x11vnc ];
          nativeBuildInputs = with pkgs; [ pkg-config gnumake ];
          buildPhase = ''
            make CC=gcc PKG_CFLAGS="-I$PWD -I$PWD/vendor/xcb-errors-include -I$PWD/vendor/libxcb-errors/include $(pkg-config --cflags xcb xcb-util xcb-keysyms xcb-wm xcb-errors xcb-randr xcb-ewmh xcb-xinput)"
          '';
          installPhase = ''
            mkdir -p $out/bin
            cp wm $out/bin/
            # Create startup script that launches Xvfb, x11vnc, and wm
            cat > $out/bin/start.sh << 'STARTSCRIPT'
#!/bin/sh
XVFB=$(find /nix/store -name Xvfb -type f 2>/dev/null | head -1)
X11VNC=$(find /nix/store -name x11vnc -type f 2>/dev/null | head -1)
if [ -x "$XVFB" ]; then exec "$XVFB" :0 -screen 0 1024x768x24; fi
if [ -x "$X11VNC" ]; then "$X11VNC" -display :0 -forever -shared & fi
sleep 1
DISPLAY=:0 /wm/bin/wm
STARTSCRIPT
            chmod +x $out/bin/start.sh
          '';
        };
      }
    );
}