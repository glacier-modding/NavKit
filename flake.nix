{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  # vcpkg baseline ports (e.g. tiff) break under CMake 4
  inputs.nixpkgs-cmake3.url = "github:NixOS/nixpkgs/nixos-25.05";

  outputs = { self, nixpkgs, nixpkgs-cmake3 }:
    let
      systems = [ "x86_64-linux" ];
      forEach = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forEach (system:
        let
          pkgs = import nixpkgs { inherit system; };
          cmake3 = nixpkgs-cmake3.legacyPackages.${system}.cmake;
          xorgLibs = with pkgs; [
            libx11 libxext libxrandr libxi libxcursor libxinerama libxxf86vm
            libxscrnsaver libxmu libxrender libxfixes libxtst libxcb libxdmcp libxau
          ];
        in {
          default = pkgs.mkShell {
            packages = with pkgs; [
              cmake3 ninja pkg-config git curl zip unzip gnutar perl (python3.withPackages (ps: [ ps.jinja2 ps.setuptools ]))
              autoconf autoconf-archive automake libtool bison flex nasm
              libGL libGLU mesa libxkbcommon wayland wayland-scanner wayland-protocols
              llvmPackages_19.clang-tools
            ] ++ xorgLibs;

            shellHook = ''
              export VCPKG_ROOT="$PWD/extern/vcpkg"
              export VCPKG_DISABLE_METRICS=1
              # meson replaces PKG_CONFIG_PATH with vcpkg's dirs; LIBDIR stands in for a distro's default search path
              export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
              [ -x "$VCPKG_ROOT/vcpkg" ] || "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

              # Runtime-only libs a distro would have in /usr/lib: libstdc++ for vcpkg's rpath-stripped host tools, libdbus for meson run checks, mesa since non-NixOS has no /run/opengl-driver
              export LD_LIBRARY_PATH="${pkgs.stdenv.cc.cc.lib}/lib:${pkgs.dbus.lib}/lib:${pkgs.mesa}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
              export LIBGL_DRIVERS_PATH="${pkgs.mesa}/lib/dri"
              export __EGL_VENDOR_LIBRARY_FILENAMES="${pkgs.mesa}/share/glvnd/egl_vendor.d/50_mesa.json"
            '';
          };
        });
    };
}
