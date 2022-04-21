#!/usr/bin/env bash

set -euo pipefail

destdir="$1"
srcdir="${PWD}"
user=$(whoami)

sudo apt-get update
sudo apt-get install -y \
  libpcre2-dev \
  libtool \
  libuv1-dev \
  libxml2-dev \
  shtool

PHP_VERSION="${PHP_VERSION:-8.0.17}"
PHP_SHA256="${PHP_SHA256:-bdbd792901c156c4d1710c9d266732d3c17f6ff63850d6660b9d8d3411188424}"
phptar="php-${PHP_VERSION}.tar.gz"

cd /tmp
curl -OL https://www.php.net/distributions/${phptar}
echo "${PHP_SHA256}  ${phptar}" | sha256sum -c

tar -xf "${phptar}"

cd "/tmp/php-${PHP_VERSION}"
./configure --disable-debug --disable-zts \
  --with-layout=GNU \
  --prefix="/opt/php/${PHP_VERSION}" \
  --with-config-file-scan-dir="/opt/php/${PHP_VERSION}/etc/php/conf.d" \
  --disable-all

make -j $(nproc)
sudo make install
PATH="/opt/php/${PHP_VERSION}/bin:$PATH"

cd /tmp
curl -OL https://github.com/DataDog/libddprof/releases/download/v0.6.0/libddprof-x86_64-unknown-linux-gnu.tar.gz
sha256sum="8eaec92d14bcfa8839843ba2ddfeae254804e087a4984985132a508d6f841645"
echo "$sha256sum  libddprof-x86_64-unknown-linux-gnu.tar.gz" | sha256sum -c
export DDProf_ROOT="/opt/libddprof"
sudo mkdir -p "$DDProf_ROOT"
sudo chown "$user" "$DDProf_ROOT"
tar -x --strip-components 1 \
  -f libddprof-x86_64-unknown-linux-gnu.tar.gz \
  -C "$DDProf_ROOT"

curl -OL https://github.com/Kitware/CMake/releases/download/v3.22.4/cmake-3.22.4-linux-x86_64.tar.gz
sha256sum=""bb70a78b464bf59c4188250f196ad19996f2dafd61c25e7c07f105cf5a95d228
echo "$sha256sum  cmake-3.22.4-linux-x86_64.tar.gz"
sudo mkdir -p /opt/cmake
sudo chown $user /opt/cmake
tar -x --strip-components 1 \
  -f cmake-3.22.4-linux-x86_64.tar.gz \
  -C /opt/cmake
PATH="/opt/cmake/bin:$PATH"

mkdir -p "/tmp/build-profiling"
cd "/tmp/build-profiling"
cmake -S "${srcdir}" -B . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=off \
  -DCMAKE_C_VISIBILITY_PRESET='hidden' \
  -DCMAKE_FIND_LIBRARY_SUFFIXES=".a;.so"
cmake --build . --parallel $(nproc)

prefix="$(php-config --prefix)"
extension_dir="$(php-config --extension-dir)"
expanded="artifacts/datadog-profiling/${extension_dir#$prefix/}"
mkdir -v -p "${expanded}"
cp -v datadog-profiling.so "${expanded}/"

cd artifacts

tar -zcvf "${destdir}/datadog-profiling.tar.gz" "datadog-profiling"

