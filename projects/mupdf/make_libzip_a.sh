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
INSTALL_DIR="$PREFIX/install"
mkdir -p "$PREFIX"
mkdir -p "$INSTALL_DIR"

cd $PREFIX

#––– Number of make jobs –––
JOBS=${JOBS:-$(nproc)}

download_and_extract() {
    local url="$1"
    wget -q "$url" -O - | tar xzf -
}

#––– 1) Build zlib –––
download_and_extract "https://zlib.net/zlib-${ZLIB_VER}.tar.gz"
pushd zlib-${ZLIB_VER}
./configure --static --prefix="$INSTALL_DIR"
make -j"$JOBS"  CFLAGS="-fPIC" install
popd

#––– 2) Build bzip2 –––
download_and_extract "https://sourceware.org/pub/bzip2/bzip2-${BZIP2_VER}.tar.gz"
pushd bzip2-${BZIP2_VER}
make -j"$JOBS"  CFLAGS="-fPIC"
make PREFIX="$INSTALL_DIR" install
popd

#––– 3) Build xz/liblzma –––
download_and_extract \
  "https://tukaani.org/xz/xz-${XZ_VER}.tar.gz"
pushd xz-${XZ_VER}
./configure --disable-shared --enable-static --prefix="$INSTALL_DIR" CFLAGS="-fPIC"
make -j"$JOBS"  CFLAGS="-fPIC" install
popd

#––– 4) Build OpenSSL –––
download_and_extract \
  "https://www.openssl.org/source/openssl-${OPENSSL_VER}.tar.gz"
pushd openssl-${OPENSSL_VER}
./Configure no-shared no-tests no-ssl --prefix="$INSTALL_DIR" CFLAGS="-fPIC"
make -j"$JOBS" 
make install_sw
popd

# ––– 4.1) Build zstd –––
download_and_extract \
  "https://github.com/facebook/zstd/releases/download/v1.5.5/zstd-1.5.5.tar.gz"
pushd zstd-1.5.5
make -j"$JOBS" PREFIX="$INSTALL_DIR" CFLAGS="-fPIC" install
popd

wget -q "https://archive.hadrons.org/software/libmd/libmd-1.1.0.tar.xz" -O - | tar xJf -
pushd libmd-1.1.0
CFLAGS="-fPIC" ./configure --prefix="$INSTALL_DIR" --disable-shared --enable-static CFLAGS="-fPIC"
make -j"$JOBS"
make install
popd

#––– 4.2) Build libbsd (for arc4random) –––
wget -q "https://libbsd.freedesktop.org/releases/libbsd-0.11.7.tar.xz" -O - | tar xJf - # special treatment because this is xz...
pushd libbsd-0.11.7
CPPFLAGS="-I${INSTALL_DIR}/include" CFLAGS="-fPIE" LDFLAGS="-L${INSTALL_DIR}/lib -static" ./configure --disable-shared --enable-static --prefix="$INSTALL_DIR" CFLAGS="-fPIE" 
make -j"$JOBS" install 
popd

# ––– 5) Build libzip against our deps –––
download_and_extract \
  "https://libzip.org/download/libzip-${LIBZIP_VER}.tar.gz"
pushd libzip-${LIBZIP_VER}
mkdir -p build && cd build
PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig" \
cmake .. \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DBUILD_SHARED_LIBS=OFF \
    -DZLIB_LIBRARY="$INSTALL_DIR/lib/libz.a" \
    -DZLIB_INCLUDE_DIR="$INSTALL_DIR/include" \
    -DBZIP2_LIBRARY="$INSTALL_DIR/lib/libbz2.a" \
    -DBZIP2_INCLUDE_DIR="$INSTALL_DIR/include" \
    -DLIBLZMA_LIBRARY="$INSTALL_DIR/lib/liblzma.a" \
    -DLIBLZMA_INCLUDE_DIR="$INSTALL_DIR/include" \
    -DOPENSSL_ROOT_DIR="$INSTALL_DIR" \
    -DOPENSSL_LIBRARIES="$INSTALL_DIR/lib" \
    -DOPENSSL_INCLUDE_DIR="$INSTALL_DIR/include" \
    -DZSTD_LIBRARY="$INSTALL_DIR/lib/libzstd.a" \
    -DZSTD_INCLUDE_DIR="$INSTALL_DIR/include" \
    -DLIBBSD_LIBRARY="$INSTALL_DIR/lib/libbsd.a" \
    -DLIBBSD_INCLUDE_DIR="$INSTALL_DIR/include" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
make -j"$JOBS" install
popd

#––– 6) Merge all .a into one –––
cd "$INSTALL_DIR/lib"
rm -f libzip_all.a

# extract libzip + deps
for lib in libzip.a libz.a libbz2.a liblzma.a libcrypto.a libssl.a libzstd.a libbsd.a libmd.a; do
    lib_path=$(find "$INSTALL_DIR" -name "$lib" -type f | head -n 1)
    if [ -z "$lib_path" ]; then
        echo "Error: $lib not found in $INSTALL_DIR" >&2
        exit 1
    fi
    ar x "$lib_path"
done

# re-archive
ar rcs libzip_all.a *.o
ranlib libzip_all.a

echo "✅ Built combined library:"
echo "   $INSTALL_DIR/lib/libzip_all.a"

cp "$INSTALL_DIR/lib/libzip_all.a" $WORK/
