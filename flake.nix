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
          propagatedBuildInputs = with pkgs; [
            xvfb
            x11vnc
          ];
          nativeBuildInputs = with pkgs; [
            pkg-config
            gnumake
          ];
          buildPhase = ''
            make
          '';
          installPhase = ''
            mkdir -p $out/bin
            cp wm $out/bin/
            # Create startup script that launches Xvfb, x11vnc, and wm
            cat > $out/bin/start.sh << STARTSCRIPT
#!/bin/sh
${pkgs.xvfb}/bin/Xvfb :0 -screen 0 1024x768x24 &
${pkgs.x11vnc}/bin/x11vnc -display :0 -forever -shared &
sleep 1
DISPLAY=:0 /wm/bin/wm
STARTSCRIPT
            chmod +x $out/bin/start.sh
          '';
        };
      }
    );
}
