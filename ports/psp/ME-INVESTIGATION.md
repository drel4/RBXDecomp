# Media Engine investigation — 2026-11-12

## Current decision

The shipped runtime path remains on the main Allegrex CPU plus GU/GUM VFPU.
**No Media Engine acceleration is implemented or claimed.** This is not a
second thread renamed as an ME worker. Do not enable an unverified kernel bridge
in the playable build merely to report that the coprocessor is in use.

Crossroads' settled player/world update currently costs roughly 5–7 ms in the
64 MB emulator; periodic instance collection takes about 5–6 ms. These are
emulator measurements, not hardware timings or a demonstrated ME speedup.

## Reviewed implementations

- [Custom Core](https://github.com/mcidclan/psp-media-engine-custom-core): its
  own documentation describes device-dependent mapping tables, an embedded
  kernel PRX and sceMeRpc patching, plus suspend handling. Therefore integrating
  it is a platform/firmware compatibility task, not just recompiling gameplay
  with a MIPS compiler. No code from that project has been incorporated here.
- [Safe Task](https://github.com/mcidclan/psp-media-engine-safe-task): its README
  redirects new users to MIST. It describes asynchronous task dispatch and a
  wait-ready mechanism, and requires the custom processing loop to cooperate
  with sleep/wake and other ME syscalls. This matters for future audio use.
- [snes9xTYL's ME interface](https://github.com/esmjanus/snes9xTYL/blob/mecm/psp/mediaengineprx/me.h)
  and [dispatch implementation](https://github.com/esmjanus/snes9xTYL/blob/mecm/psp/mediaengineprx/me.c):
  jobs carry input/output cache ranges, completion state and a return value.
  Its busy-wait interface is evidence of the coordination involved, not a
  suitable drop-in scheduler for this client's 30 FPS loop.

## Candidate work and acceptance criteria

Independent, immutable geometry/culling batches are better initial candidates
than Lua, instance ownership or the sequential character/contact solve. That is
an architectural inference, not a benchmark result. A prospective job must use
bounded flat buffers rather than live STL containers or DataModel pointers.

Before accepting an ME backend:

1. Profile the candidate on a physical PSP-3008; include transfer/cache and
   synchronization costs, not only the inner arithmetic loop.
2. Use aligned, explicitly owned buffers and the correct cache writeback and
   invalidation on both processors. Publish a completion sequence only after
   output visibility is guaranteed; `volatile` alone is not that guarantee.
3. Handle initialization failure, timeout, suspend/resume and audio coexistence.
   Keep the main-CPU implementation as the functional fallback.
4. Verify matching results against the CPU implementation over varied scenes,
   demonstrate actual ME execution on hardware, and measure an end-to-end gain.

No physical PSP is available to this session. Consequently there is no verified
ME backend to package. GU rendering and main-CPU operation remain independent of
ME availability; no extra kernel ME module is currently required.
