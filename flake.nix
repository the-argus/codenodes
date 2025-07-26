{
  description = "dev environment for codenodes";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs?ref=nixos-25.05";
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
        # NOTE: clang-tools has to come before clang, and we cant use
        # clangStdenv because something is messed up with the setup hooks and it
        # cant find the c runtime libs. so instead it has to be installed as
        # just a package. see
        # https://blog.kotatsu.dev/posts/2024-04-10-nixpkgs-clangd-missing-headers/
        # and https://github.com/NixOS/nixpkgs/issues/76486
        pkgs.mkShellNoCC
        {
          packages = with pkgs; [
            clang-tools
            clang
            cmake
            gnumake
            libclang
            libllvm
            libffi
            libxml2
            (pkgs.writeShellScriptBin "configure" ''
              cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug -S . -B out -G "Unix Makefiles"
            '')
            (pkgs.writeShellScriptBin "build" ''
              cmake --build out --parallel
            '')
            (pkgs.writeShellScriptBin "run" ''
              ./out/codenodes $@
            '')
            (pkgs.writeShellScriptBin "debug" ''
              gdb --args ./out/codenodes $@
            '')
          ];
        };
    });
}
