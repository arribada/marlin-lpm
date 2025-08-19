{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux"; # change to: aarch64-linux, x86_64-darwin, aarch64-darwin
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = [
          pkgs.protobuf
          (pkgs.python3.withPackages (
            ps: with ps; [
              protobuf
              pyserial
            ]
          ))
        ];
      };
    };
}
