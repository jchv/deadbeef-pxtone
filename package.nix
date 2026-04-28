{
  lib,
  stdenv,
  pkg-config,
  deadbeef,
  libvorbis,
  libogg,
}:

stdenv.mkDerivation {
  pname = "deadbeef-pxtone";
  version = "unstable";

  src = ./.;

  nativeBuildInputs = [ pkg-config ];

  buildInputs = [
    deadbeef
    libvorbis
    libogg
  ];

  enableParallelBuilding = true;

  buildFlags = [
    "DEADBEEF_ROOT=${deadbeef}"
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/lib/deadbeef/
    cp *.so $out/lib/deadbeef/

    runHook postInstall
  '';

  meta = with lib; {
    description = "Pxtone Collage music plugin for DeaDBeeF";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
