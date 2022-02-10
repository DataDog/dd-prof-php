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

PHP_VERSION="${PHP_VERSION:-8.0.15}"
PHP_SHA256="${PHP_SHA256:-47f0be6188b05390bb457eb1968ea19463acada79650afc35ec763348d5c2370}"
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
curl -OL https://github.com/DataDog/libddprof/releases/download/v0.3.0/libddprof-x86_64-unknown-linux-gnu.tar.gz
sha256sum="d9c64567e7ef5f957581dd81892b144b81e1f52fdf5671430c7af0b039b48929"
echo "$sha256sum  libddprof-x86_64-unknown-linux-gnu.tar.gz" | sha256sum -c
export DDProf_ROOT="/opt/libddprof"
sudo mkdir -p "$DDProf_ROOT"
sudo chown "$user" "$DDProf_ROOT"
tar -x --strip-components 1 \
  -f libddprof-x86_64-unknown-linux-gnu.tar.gz \
  -C "$DDProf_ROOT"

curl -OL https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-linux-x86_64.tar.gz
sha256sum="38b3befdee8fd2bac06954e2a77cb3072e6833c69d8cc013c0a3b26f1cfdfe37"
echo "$sha256sum  cmake-3.22.2-linux-x86_64.tar.gz"
sudo mkdir -p /opt/cmake
sudo chown $user /opt/cmake
tar -x --strip-components 1 \
  -f cmake-3.22.2-linux-x86_64.tar.gz \
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

