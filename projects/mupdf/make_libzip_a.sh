#!/usr/bin/env bash
set -xeuo pipefail

#––– Versions –––
ZLIB_VER="1.3.1"
BZIP2_VER="1.0.8"
XZ_VER="5.6.2"
OPENSSL_VER="3.1.3"
LIBZIP_VER="1.10.0"

#––– Installation prefix –––
PREFIX="$PWD/deps"
mkdir -p "$PREFIX"

#––– Number of make jobs –––
JOBS=${JOBS:-$(nproc)}

download_and_extract() {
    local url="$1"
    wget -q "$url" -O - | tar xzf -
}

# #––– 1) Build zlib –––
# wget -q "https://zlib.net/zlib-${ZLIB_VER}.tar.gz" -O - | tar xzf -
# pushd zlib-${ZLIB_VER}
# ./configure --static --prefix="$PREFIX"
# make -j"$JOBS"  CFLAGS="-fPIE" LDFLAGS="-pie" install
# popd

# #––– 2) Build bzip2 –––
# download_and_extract "https://sourceware.org/pub/bzip2/bzip2-${BZIP2_VER}.tar.gz"
# pushd bzip2-${BZIP2_VER}
# make -j"$JOBS"  CFLAGS="-fPIE" LDFLAGS="-pie"
# make PREFIX="$PREFIX" install
# popd

# #––– 3) Build xz/liblzma –––
# download_and_extract \
#   "https://tukaani.org/xz/xz-${XZ_VER}.tar.gz"
# pushd xz-${XZ_VER}
# ./configure --disable-shared --enable-static --prefix="$PREFIX"
# make -j"$JOBS"  CFLAGS="-fPIE" LDFLAGS="-pie" install
# popd

#––– 4) Build OpenSSL –––
download_and_extract \
  "https://www.openssl.org/source/openssl-${OPENSSL_VER}.tar.gz"
pushd openssl-${OPENSSL_VER}
./Configure no-shared no-tests --prefix="$PREFIX" \
  linux-x86_64 CFLAGS="-fPIE" LDFLAGS="-pie"
make -j"$JOBS"  
make install_sw
popd

# ––– 5) Build libzip against our deps –––
download_and_extract \
  "https://libzip.org/download/libzip-${LIBZIP_VER}.tar.gz"
pushd libzip-${LIBZIP_VER}
mkdir -p build && cd build
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" \
cmake .. \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DZLIB_LIBRARY="$PREFIX/lib/libz.a" \
    -DZLIB_INCLUDE_DIR="$PREFIX/include" \
    -DBZIP2_LIBRARY="$PREFIX/lib/libbz2.a" \
    -DBZIP2_INCLUDE_DIR="$PREFIX/include" \
    -DLIBLZMA_LIBRARY="$PREFIX/lib/liblzma.a" \
    -DLIBLZMA_INCLUDE_DIR="$PREFIX/include" \
    -DOPENSSL_ROOT_DIR="$PREFIX" \
    -DOPENSSL_LIBRARIES="$PREFIX/lib" \
    -DOPENSSL_INCLUDE_DIR="$PREFIX/include"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$JOBS" install
popd

#––– 6) Merge all .a into one –––
cd "$PREFIX/lib"
rm -f libzip_all.a

# extract libzip + deps
for lib in libzip.a libz.a libbz2.a liblzma.a libcrypto.a libssl.a; do
    lib_path=$(find "$PREFIX" -name "$lib" -type f | head -n 1)
    if [ -z "$lib_path" ]; then
        echo "Error: $lib not found in $PREFIX" >&2
        exit 1
    fi
    ar x "$lib_path"
done

# re-archive
ar rcs libzip_all.a *.o
ranlib libzip_all.a

echo "✅ Built combined library:"
echo "   $PREFIX/lib/libzip_all.a"
