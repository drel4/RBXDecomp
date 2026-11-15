# Validation of the notblox patch branch

These are modified builds, not a claim of binary identity with December 2007
Roblox. The upstream reconstruction documentation is retained for reference.

Checked on 2026-11-15:

- Clean MSVC 8.0 SP1 builds of the regular Windows client and shared headless
  multiplayer/thumbnail worker.
- Current MSVC builds of the `app` and `network` targets, with clang-tidy enabled.
- Clean native amd64 Linux server build; legacy wire-width, monotonic network
  clock, and real Lua/30 Hz gravity-and-collision checks pass.
- 37 isolated website/account/community tests pass. These cover mutual friends,
  specific-server joins, friends-only room visibility and authorization, ticket
  and live-access revocation, moderation, private image access, avatar selection,
  and owner/admin server controls.
- The compiled regular client loads the February Crossroads copy, equips and
  attacks with its locally embedded sword, with the asset proxy disabled.
- Source annotation lint passes for the main engine/client paths, and the
  address-padding check passes. The original-client vtable comparison reports
  134 vtables and a 100% match.

Not all upstream reconstruction diagnostics are green: data comparison reports
60 original-client variable differences; folded-function checks report alias
differences; direct RakNet annotation lint reports function-order errors. Naming
analysis was stopped after reporting the legacy `DECLARE_LIBID()` macro-name
diagnostic; a complete clean naming pass is not claimed. These are documented rather
than presented as proof that this patch branch remains byte-exact.

The experimental PSP port has its own status and acceptance gates in
`ports/psp/README.md` and `ports/psp/PROGRESS.md`; the checks above do not claim
that port is complete. Original client content, runtime databases, credentials
and generated binaries are not distributed as part of these changes.
