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
        (pkgs.mkShell.override {stdenv = pkgs.clangStdenv;})
        {
          packages = with pkgs; [
            cmake
            gnumake
            libclang
            libllvm
            libffi
            libxml2
            (pkgs.writeShellScriptBin "configure" ''
              cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B out -G "Unix Makefiles"
            '')
            (pkgs.writeShellScriptBin "build" ''
              cmake --build out --parallel
            '')
            (pkgs.writeShellScriptBin "run" ''
              ./out/codenodes $@
            '')
          ];
        };
    });
}
