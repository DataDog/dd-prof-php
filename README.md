# Datadog Continuous Profiler for PHP

The Datadog PHP profiler is a Zend Extension for PHP 7.1+. Debug and ZTS builds
are not currently supported.

The plan for this code is to be eventually merged into
[Datadog/dd-trace-php](https://github.com/DataDog/dd-trace-php).

## Installing

Users should install via the Tracer's installations script.
TODO: add more details about this.

### Building From Source

For people who really want to build from source, like other Datadog Engineers,
you will need:

 - CMake 3.19 or newer to generate the build
 - Make or Ninja
 - C11 compiler
 - pkg-config
 - PHP 7.1+
 - libuv (todo: min version?) and its headers
 - libddprof v0.2 for your platform. This is a Rust library, so I recommend
   using the pre-built artifacts available at
   [Datadog/libddprof](https://github.com/DataDog/libddprof/releases) so you
   don't need a Rust toolchain.

Here's an example of how to build it, based on my own development machine:

```bash
# Set up environment so cmake can find libuv and libddprof
LIBUV_ROOT="$(readlink -e $(brew --prefix libuv))"
export DDProf_ROOT="/opt/libddprof"

export PKG_CONFIG_PATH="$(find ${LIBUV_ROOT?} -name pkgconfig -type d):$PKG_CONFIG_PATH"

# Build profiler in Release configuration
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build cmake-build-release

# Run diagnostics
DD_PROFILING_ENABLED=yes php \
    -dzend_extension=cmake-build-release/datadog-profiling.so \
    --ri datadog-profiling

# Optional: Install the profiler
export PHP_INI_DIR="$(php-config --ini-dir)"
export PHP_EXTENSION_DIR="$(php-config --extension-dir)"
cp -v cmake-build-release/datadog-profiling.so "${PHP_EXTENSION_DIR?}/"
echo 'zend_extension = datadog-profiling.so' > "${PHP_INI_DIR?}/datadog-profiling.ini"
```
