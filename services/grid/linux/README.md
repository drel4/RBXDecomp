# Native multiplayer worker

Only the game server is ported. Thumbnail jobs keep using the existing
`grid-worker.exe` under Wine, with its original renderer.

The native target compiles the same engine, Lua 5.1, physics and RakNet sources
as the Windows worker. Both i386 and amd64 builds keep the legacy protocol's
integer widths independently of host pointer size. Neither changes simulation
frequency or replaces physics with a simplified implementation.

## Build

From the repository root on a Linux Docker host, for i386:

```sh
docker build -t openrbx-native-builder -f services/grid/linux/Dockerfile.build .
docker run --rm -v "$PWD:/src" -w /src openrbx-native-builder sh -c '
  cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 &&
  cmake --build build-native --parallel 3
'
```

For amd64, use matching development libraries and a separate build directory:

```sh
docker build --build-arg NATIVE_ARCH=amd64 -t openrbx-native-builder-amd64 \
  -f services/grid/linux/Dockerfile.build .
docker run --rm -v "$PWD:/src" -w /src openrbx-native-builder-amd64 sh -c '
  cmake -S . -B build-native-amd64 -G Ninja -DCMAKE_BUILD_TYPE=Release &&
  cmake --build build-native-amd64 --parallel 3
'
```

No editor, DirectX, Windows SDK, Wine headers or Wine runtime are used for this
target. Native adapters provide POSIX filesystem/threading/time, OpenSSL hashing
and random GUIDs, and libcurl HTTP. Server audio output is disabled; original
Sound instances and their replicated properties remain available.

## Runtime selection

Install `build-native/grid-server` alongside `runtime/grid-worker.exe` and
`runtime/content/`. Keep the Wine executable and its content for thumbnails.

`GRID_SERVER_BACKEND=native` explicitly selects Linux for game rooms; `wine`
is the rollback option. The default `auto` uses native when `grid-server` is
installed, otherwise Wine. Native place/content/status paths are normal POSIX
paths. Only Wine jobs receive `Z:` paths.

The native server uses the same UDP ports, heartbeat/status files, account
access files as the Windows worker.

Rebuild the service image with `services/grid/Dockerfile` when installing the
native binary: it includes the required i386/amd64 Linux runtime libraries while
retaining Wine for thumbnails. `/health` reports the selected game backend and
the independent thumbnail backend. Keep the previous runtime/image available
and select `GRID_SERVER_BACKEND=wine` to roll back.

## Verification

With the original content mounted at `/runtime/content`, run:

```sh
build-native/grid-clock-test
build-native/grid-protocol-test
build-native/grid-simulation-test /runtime/content
```

These exercise concurrent monotonic RakNet clocks, real Lua/30 Hz simulation
with gravity and collisions, fixed-width marker/compressed-double packets,
string-length encoding, GUID ordering across scopes and aligned engine allocations.
The deployed worker was also checked with two Windows clients for
joins, object/property replication, deletion, death/respawn, account-based
website launches, player telemetry and fresh-room idle shutdown. The website's
account, coordinator and community tests pass with native game routing and Wine
thumbnail routing.

Use `build-native-amd64/` instead of `build-native/` for the amd64 test binaries.
The amd64 build has also passed the engine tests and two-Windows-client
join, replication, deletion and death/respawn checks. The existing thumbnail
generator remains the original Windows executable under Wine; no amd64
thumbnail generator is built.
