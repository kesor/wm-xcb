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
        let
          # The libxcb headers and man pages ship without the implementation.
          # Whether xcb_poll_for_reply() waits, and whether a serial comparison
          # masks XCB_REQUEST_CHECKED, are in the unpacked source, not in the
          # header. Unpacked here so that answer comes out of the store instead
          # of a fetch from gitlab. docs/TESTING.md names the files.
          libxcb-source = pkgs.stdenv.mkDerivation {
            pname = "libxcb-source";
            inherit (pkgs.libxcb) version src;
            dontConfigure = true;
            dontBuild = true;
            installPhase = "cp -a . $out";
          };
        in
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
              export DISPLAY=:99
              ${pkgs.lib.getExe pkgs.xvfb-run} ${pkgs.lib.getExe pkgs.x11vnc} -display :99 -forever -shared &
              sleep 2
              ${pkgs.lib.getExe pkgs.xterm} &
              /wm/bin/wm
              EOF
              chmod +x $out/bin/start.sh
            '';
          };

          devShells."${system}".default = pkgs.mkShell {
            LIBXCB_SRC = "${libxcb-source}";

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

              # Nested-X testing and measurement. See docs/TESTING.md.
              xorg-server # provides Xephyr and Xvfb
              xwininfo
              xvfb-run
              xprop
              xrandr
              xmodmap # which keycode carries each modifier bit; a keymap dump does not list them
              xset # the seam suite reads the NumLock state from it
              xdotool
              xterm
              dmenu
              imagemagick
              valgrind
              strace

              # Quality gates: .beads/hooks/* call 'lefthook run <hook>'.
              # See docs/QUALITY.md.
              lefthook
              actionlint # scripts/check-workflow.sh parses .github/workflows/
              shellcheck # scripts/check-shellcheck.sh; its baseline is pinned to this version
            ];
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
