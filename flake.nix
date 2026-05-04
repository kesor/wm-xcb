{
  description = "wm-xcb X11 window manager";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" ];
      perSystem =
        { pkgs, system }:
        {
          packages."${system}".default = pkgs.stdenv.mkDerivation {
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
              xvfb-run
              x11vnc
            ];
            nativeBuildInputs = with pkgs; [
              pkg-config
              gnumake
            ];
            buildPhase = "make";
            installPhase = ''
              mkdir -p $out/bin
              cp wm $out/bin/
              cat > $out/bin/start.sh << EOF
              #!/usr/bin/env sh
              ${pkgs.lib.getExe pkgs.xvfb-run} ${pkgs.lib.getExe pkgs.x11vnc} -display :99 -forever -shared &
              sleep 2
              DISPLAY=:99 /wm/bin/wm
              EOF
              chmod +x $out/bin/start.sh
            '';
          };

          devShells."${system}".default = pkgs.mkShell {
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
        };

      outputsForSystem =
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        perSystem { inherit pkgs system; };

      combinedOutputs = builtins.foldl' (acc: system: acc // outputsForSystem system) { } systems;
    in
    combinedOutputs;
}
