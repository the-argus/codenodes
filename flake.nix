{
  description = "dev environment for codenodes";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs?ref=nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    nixpkgs,
    flake-utils,
    ...
  }: let
    supportedSystems = let
      inherit (flake-utils.lib) system;
    in [
      system.aarch64-linux
      system.aarch64-darwin
      system.x86_64-linux
    ];
  in
    flake-utils.lib.eachSystem supportedSystems (system: let
      pkgs = import nixpkgs {inherit system;};
    in {
      devShell =
        (pkgs.mkShell.override { stdenv = pkgs.clangStdenv; })
        {
          packages =
            (with pkgs; [
              (pkgs.writeShellScriptBin "configure" "cmake -S . -B build")
              (pkgs.writeShellScriptBin "build" "cmake --build build --parallel")
              zig_0_14
              cmake
              gnumake
              libclang
              libllvm
              libxml2
              libffi
              bear
            ]) ++ pkgs.lib.optionals (system != flake-utils.lib.system.aarch64-darwin) (with pkgs; [
              gdb
              valgrind
	        ]);

           shellHook = ''
            export LIBCLANG_INCLUDE_DIR=${pkgs.libclang.dev}/include/
            export PATH=${pkgs.clang}/bin:$PATH
          '';
        };

      formatter = pkgs.alejandra;
    });
}
