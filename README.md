# Datadog Continuous Profiler for PHP

The Datadog PHP profiler is a Zend Extension for PHP 7.1+. Debug and ZTS builds
are not currently supported.

Supported platforms (all x86-64):
 - CentOS 7+ GNU/Linux. This works for most glibc based Linux versions that have
   glibc 2.17 or newer.
 - Alpine 3.13+ with musl.

Additionally, it has been developed and tested on MacOS Big Sur (11.6).

The plan for this code is to be eventually merged into
[Datadog/dd-trace-php](https://github.com/DataDog/dd-trace-php).

## Installing

See [enabling the profiler](https://docs.datadoghq.com/tracing/profiler/enabling/php/).

## Configuring

The profiler uses these environment variables. If the tracer is already set up,
then you may not need to adjust any of them:

 - `DD_PROFILING_ENABLED`: defaults to `false`.
 - `DD_PROFILING_LOG_LEVEL`: defaults to `off`. Acceptable values are `off`,
   `error`, `warn`, `info`, and `debug`. Log message are printed to stderr, not
   to the PHP `error_log`.
 - `DD_PROFILING_EXPERIMENTAL_CPU_TIME_ENABLED`: defaults to `false`, as it is
   experimental. It has low overhead, but is biased towards functions that do
   I/O.
 - `DD_ENV`: defaults to the empty string.
 - `DD_SERVICE`: defaults to the empty string. If not set, this will become
   `unnamed-php-service` in the Datadog UI.
 - `DD_VERSION`: defaults to the empty string.
 - `DD_AGENT_HOST`: defaults to `localhost`.
 - `DD_TRACE_AGENT_PORT`: defaults to `8126`.
 - `DD_TRACE_AGENT_URL`: defaults to the empty string. If set, this will
   override `DD_AGENT_HOST` and `DD_TRACE_AGENT_PORT`.

### Building From Source

For people who really want to build from source, like other Datadog Engineers,
you will need:

 - CMake 3.19 or newer to generate the build
 - Make or Ninja
 - C11 compiler
 - pkg-config
 - PHP 7.1+
 - libuv (todo: min version?) and its headers
 - libddprof v0.3 for your platform. This is a Rust library, so I recommend
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
