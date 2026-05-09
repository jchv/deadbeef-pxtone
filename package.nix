{
  lib,
  stdenv,
  pkg-config,
  deadbeef,
  libvorbis,
  libogg,
}:

stdenv.mkDerivation {
  pname = "deadbeef-pxtone-plugin";
  version = "0-unstable";

  src = ./.;

  nativeBuildInputs = [ pkg-config ];

  buildInputs = [
    deadbeef
    libvorbis
    libogg
  ];

  enableParallelBuilding = true;

  makeFlags = [ "DEADBEEF_ROOT=${deadbeef}" ];
  installFlags = [ "DEADBEEF_ROOT=$(out)" ];

  meta = with lib; {
    description = "Pxtone Collage music decoder plugin for the DeaDBeeF music player";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
