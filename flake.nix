{
  description = "Game Render Test";

  inputs = {
    nixpkgs.url = "nixpkgs/nixos-24.05";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          runInputs = with pkgs; [
            SDL2
          ];

          buildInputs = with pkgs; runInputs ++ [
            cmake
            pkg-config
            makeWrapper
            bashInteractive
          ];

        in
        with pkgs; {
          # devShell = mkShell {
          #   inherit buildInputs;


          #   LOCALE_ARCHIVE = "${glibcLocales}/lib/locale/locale-archive";
          #   hardeningDisable = ["all"];

          #   shellHook = ''
          #     source $PWD/enable
          #   '';
          # };

          packages.default = pkgs.stdenv.mkDerivation {
            pname = "ren";
            version = "0.1.0";

            src = pkgs.nix-gitignore.gitignoreSource [] ./.;

            inherit buildInputs;

            configurePhase = ''
                cmake -B build -S . \
                -DCMAKE_BUILD_TYPE=Release \
                -DPKG_CONFIG_PATH="${pkgs.SDL2}/lib/pkgconfig"
            '';

            buildPhase = ''
                cmake --build build
            '';

            installPhase = ''
                mkdir -p $out/bin
                cp build/mono_embedded $out/bin/
            '';
          };
        }
      );
}
