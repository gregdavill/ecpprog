{
  description = "A basic driver for FTDI based JTAG probes (FT232H, FT2232H, FT4232H), to program Lattice ECP5/Nexus FPGAs.";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs = { self, nixpkgs, flake-utils } :
  flake-utils.lib.eachDefaultSystem (system:
    let pkgs = import nixpkgs { inherit system; };
        ecpprog = {
          name = "ecpprog";
          buildInputs = [ pkgs.clang pkgs.gnumake pkgs.libftdi1 ];
          nativeBuildInputs = [ pkgs.pkg-config ];
          src = ./ecpprog;
          buildPhase = ''
            export DESTDIR=$out
            export PREFIX=
            make
          '';
          installPhase = ''
            export DESTDIR=$out
            export PREFIX=
            make install
          '';
        };
    in
    {
      devShells.default = pkgs.mkShell ecpprog;
      packages.ecpprog = with pkgs; stdenv.mkDerivation ecpprog;
      defaultPackage = with pkgs; stdenv.mkDerivation ecpprog;
    }
  );
}
