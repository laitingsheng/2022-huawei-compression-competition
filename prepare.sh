#!/usr/bin/env bash

mkdir -p code

cp -a cmake include src toolchains CMakeLists.txt  LICENCE.md code

mkdir -p code/external/{fast-lzma2,fmt,frozen,mio}

cp -a external/fast-lzma2/*.{c,h} external/fast-lzma2/LICENSE code/external/fast-lzma2

cp -a external/fmt/{include,src,support,CMakeLists.txt,ChangeLog.rst,LICENSE.rst,README.rst} code/external/fmt

cp -a external/frozen/{cmake,include,CMakeLists.txt,LICENSE} code/external/frozen

cp -a external/mio/{cmake,include,CMakeLists.txt,LICENSE} code/external/mio
