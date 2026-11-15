# notblox / RBXDecomp Grid

Basic HTML frontend, admin place/server management, and native OpenRBX engine workers.
The Linux web service **does not replace the game with a browser simulation**:
The game workers use this repository's DataModel, Lua, RakNet and physics.
Game rooms can use the Windows worker under Wine, native Linux i386, or native
Linux amd64. Thumbnails still use `grid-worker.exe` and the original
`WebService/ThumbnailGenerator.cpp` under Wine. The client renders gameplay.

## Owner workflow

1. Visit `https://blox.ralsei.tech/admin/login` and enter the existing admin password.
2. Upload a legacy XML RBXL at **Places admin**, with title and description.
3. Wait for the engine thumbnail to become **ready**; reload the page to check.
4. Press **Publish**. The place appears in the games room immediately.

No Python, configuration or source edits are needed to add places. Drafts stay
private. Unpublish hides a place; **Stop server** stops an individual room.
The next Play starts a fresh copy. Use **Edit details / update file** to replace
an existing place's RBXL without changing its URL, publication state or player
limit. The old thumbnail remains visible while a new one is generated. New
rooms use the updated map; existing rooms retain their map until restarted.
Old map files are retained while a running room or another place needs them.

Under **Server build**, choose Windows (Wine), Native Linux 32-bit, or Native
Linux 64-bit. The choice is saved across service restarts and applies to newly
started rooms. Missing builds are disabled. The server list shows the actual
build of each room; **Restart on selected build** disconnects its players and
starts a replacement room using the selected build and latest place file.
Players must rejoin. Changing the selection alone never stops active games.
Thumbnail generation always stays on Wine regardless of the game-room choice.

Installed game binary names are `runtime/grid-worker.exe`, `runtime/grid-server`
(i386), and `runtime/grid-server-amd64`. See [Linux build instructions](linux/README.md).

Players register at `/signin` (also `/register`) and log in at `/login` with a
case-insensitive unique 3-20 character nickname. Registration is invite-only by
default. Admins can open it or create batches of invite codes with per-code use
limits, expiry in minutes/hours/days (0 means no expiry), and revocation. Invite
claims and account creation are atomic even under concurrent registration.

Every signup gets a cryptographically random password displayed once; only its
salted hash is saved. Users cannot choose their own password. Sessions persist
for 30 days using HttpOnly cookies and revocable hashed database tokens. HTTPS
cookies also have the Secure flag; HTTP and HTTPS use separate cookie names.
Admins can rename users, reset passwords randomly or to an admin-chosen value,
ban/unban, and permanently remove accounts. Resetting passwords revokes sessions.
Admin login stays separate, using the existing owner password, not a public role.
All forms require CSRF. User signup/login, account settings and play work on
HTTP and HTTPS, with relative links staying on the chosen scheme. Admin login
and management remain HTTPS-only; HTTP admin requests return 403, not a redirect.
Authentication and registration attempts are rate-limited.

Play uses the logged-in nickname and stable account ID, not a form-provided name.
Join scripts use expiring single-use web tickets. Native workers read atomic
per-room access files, disconnect revoked players without stopping other players,
and apply six saved body colors, including after respawn. The legacy RakNet wire
protocol itself is not a modern secure account authentication protocol. Do not
expose a generic execute-Lua HTTP endpoint.

## Client

Use the updated Windows client, including the Blox origin/bridge changes and
Sound factory registration. `Blox.cmd` launches:

```bat
Roblox.exe -BaseUrl http://blox.ralsei.tech -Browser
```

The frontend uses `window.external.GetApp().CreateGame(2)` and `ExecUrlScript`.
Only the exact Blox frontend/API hosts are additionally trusted; wildcard DNS
hosts are **not** granted native execution privileges. Modern browsers can browse
the catalogue but do not implement the old native launch bridge.

The navigation has an English / Русский switch. A one-year language preference
cookie is independent of authentication and works on HTTP and HTTPS; without a
saved preference, the browser's language is used (English fallback). UI labels,
forms, errors and notices are translated, not player names or place content.
The Download client navigation link is hidden only when the native
`window.external.GetApp()` interface is available, including the client's
embedded browser; ordinary browsers keep the link.

HTTP `/`, `/Start.aspx` and `/IDE/Start.aspx` all stay HTTP, without an admin
sign-in link. User accounts stay on HTTP too. `-BaseUrl https://blox.ralsei.tech`
selects HTTPS instead. The launch-page API URL follows the chosen scheme.
WinINet/MSHTML keeps persistent account cookies across client restarts.

## Build native worker

Configure with the existing MSVC 8.0 SP1 environment:

```
cmake -B build-client-fast -DOPENRBX_GRID_WORKER=ON
cmake --build build-client-fast --target grid-worker roblox --parallel 8
```

Bundle `grid-worker.exe`, `fmodex.dll`, the built Mesa `OPENGL32.DLL`,
`GLU32.DLL`, `OSMESA32.DLL`, the matching Microsoft VC80 CRT assembly, and the
client's `content` folder into a runtime directory. Keep original dependency
licenses. The worker needs no editor or display; the original OSMesa thumbnail
path renders PNGs offscreen. A virtual X display supports Wine initialization.

```
grid-worker thumbnail PLACE CONTENT OUTPUT.b64
grid-worker server PLACE CONTENT UDP_PORT
```

Server mode loads the real place, starts NetworkServer, runs the real engine,
and installs a bounded per-player respawn polling coroutine. `SERVER_READY`
and periodic `GRID_STATUS` lines go to its job log. Place scripts execute with
their normal engine behavior; incomplete engine functionality is not hidden.

## Linux deployment

Current isolated directory: `/home/nyash/0drel/openrbx-grid`.

- `source/`: this directory's source and Dockerfile.
- `runtime/`: native worker, dependencies and content (read-only container mount).
- `data/`: persistent SQLite DB, original places, generated thumbnails, job logs,
  and the Wine prefix. Back this directory up with `private.env`.
- `private.env`: `GRID_SECRET` and Werkzeug-compatible `GRID_ADMIN_HASH`;
  mode 0600, never include in source, images or public downloads.
- `owner-access.txt`: initial owner credentials, mode 0600.

Build the Dockerfile and run a single web process (room ownership is in-process):

```sh
docker build -t openrbx-grid:local source
docker run -d --init --name openrbx-grid --restart unless-stopped --network host \
  --env-file private.env --memory 2g --cpus 3 --pids-limit 512 \
  -v "$PWD/data:/data" -v "$PWD/runtime:/runtime:ro" openrbx-grid:local
```

Nginx routes only the new frontend and API virtual hosts to loopback port 4380.
Both use their own Let's Encrypt certificate. The service does not alter existing
site configuration. UDP 45000вЂ“45015 is reserved for up to 16 native rooms. Both
the host firewall and any upstream NAT must permit that range; DNS alone cannot
route UDP. A real external nonce-echo test passed after adding the UFW rule.

API: `/api/places`, `/asset/<id>`, `/game/join.ashx?ticket=...`, `/health`.
Frontend compatibility aliases: `/Start.aspx`, `/IDE/Start.aspx`.
The API host serves the same application; no open proxy or user-supplied URLs.

## Checks

`python services/grid/test_app.py` verifies owner/CSRF protection, draft visibility,
publish gating, XML entity rejection, escaping, upload path isolation, nickname
collisions, and single-use tickets. Runtime validation must additionally verify
actual engine thumbnails and at least two native clients seeing the same players
and state. A web health response alone is not multiplayer acceptance.

## Remaining compatibility boundaries

Only legacy XML version-4 RBXL is accepted. Full modern place support, account
security, asset-CDN mirroring, moderation and large-scale matchmaking are not
implemented. Original places may contain obsolete asset URLs or scripts relying
on unfinished engine code. Persistent rooms are currently bounded by the 16-port
pool; there is no automatic idle expiry yet. New services must not be advertised
as fully validated multiplayer until the end-to-end checks have actually passed.


## Servers, HTTP and current validation

The public HTTP frontend has no admin link. HTTPS is required for admin pages
and actions; HTTP and HTTPS sessions use separate cookies. No client downloads
are hosted as full client builds. `/download` links to the original 0.3.676.0
archive and the online xdelta patcher, with instructions. Only the patch is
hosted locally: install `notblox.xdelta` at `data/downloads/notblox.xdelta`;
`/downloads/notblox.xdelta` serves it as an attachment on HTTP and HTTPS.
The installed local launcher is G:\OpenRBX\NotRoblox.cmd.

Each place has a 1-32 player default for **new** servers (initially 8). Admins
can request 1-16 instances, change that default, stop an individual server or stop
all servers for a place. The global pool remains 16 UDP ports. The place page
lists server IDs and occupied/reserved slots, and allows a specific server,
automatic selection or a new server. Pending reservations expire in five minutes;
consumed tickets allow 60 seconds to connect. Native RakNet enforces the actual
connection limit. Nickname uniqueness is case-insensitive per server, released
on native disconnect status (including departures not observed by a web visit).
These are still guest identities, not authenticated game accounts.

Browser launch uses the embedded type library without registration, constructs
its bridge in-process and hosts browser/game documents in the same capable client
host. HTTPS requests now set WinINet's secure flag. Default 30 FPS rendering is
configurable through Tools > Settings > Rendering > Performance > LimitTo30FPS.
Disabling it restores uncapped windowed rendering with fullscreen V-Sync; the
fixed simulation timestep is unchanged.

Live checks passed: native Linux engine thumbnail/upload/publish; two distinct
servers with the same nickname and independent capacity; HTTP guest forms with
HTTPS-only admin actions; browser CreateGame/ExecUrlScript into Crossroads; two
Windows clients replicating two players and two characters on the Linux server;
FPS setting default true and live false/true toggles; nickname reuse after a real
native disconnect; individual stop releasing its UDP port and revoking tickets.
Test clients and their servers were stopped after validation.

Thumbnail jobs first attempt normal asset loading for up to 20 seconds. On an
asset/network failure or timeout, they retry in a separate offline worker using
the engine's missing-texture fallback. Bundled and cached assets still resolve;
the uploaded RBXL is not rewritten. The retry has a 70-second render limit.

Empty servers are automatically stopped after 60 consecutive seconds without
players or connecting clients, using fresh native heartbeat data and a background
monitor independent of web requests. Connected clients reset the timer. Missing
or stale heartbeat data never counts as proof that a server is empty. Automatic
stops revoke pending tickets and release reservations just like an admin stop.

Fresh servers that have never hosted a player now use a 15-second empty timeout.
After at least one player has joined, their empty timeout becomes 60 seconds for
the rest of that server's lifetime. Native departure history records even short
visits between monitor polls; pending tickets alone do not promote the timeout.

Admin **Delete place** opens a plain HTML confirmation page. Confirming removes
the catalogue entry, thumbnail and uploaded RBXL, stops that place's servers and
revokes their tickets/reservations. Identical uploads share an RBXL: it is only
removed when the last place referencing it is deleted. Queued or in-progress
thumbnail work cannot restore a deleted place's files. Unpublish remains available
for hiding a place without deleting it.

Use **Edit title & description** beside a place in Manage places to change its
title (1-80 characters) or description (up to 2,000 characters; may be empty).
Saving does not replace the RBXL, regenerate its thumbnail, change its publication
status, or interrupt that place's running servers.

Editor Tools > Test > Solo runs the built-in in-editor player/respawn script.
1 Player and 2 Players launch separate local server/client processes with embedded
Lua. These tests do not depend on website scripts, accounts, invite codes or
player.txt. Test groups use separate high UDP ports and saved test snapshots.

## Friends and T-shirts

On a place page, **Start / join my friends-only server** creates one private
room per owner/place (or rejoins it). Only the owner and their accepted friends
can see/join it; ordinary automatic selection remains public-only. The Friends
page also links available private rooms owned by friends. Owners can stop their
private room from the place page. Removing a friendship revokes its pending
tickets and live server access; banning/removing the owner stops their private
rooms. Existing 15-second/one-minute empty-server timeouts still apply.

Signed-in users manage mutual friend requests at `/friends`. Accepted friends
show Join buttons for their actual, fresh game-server presence; reservations
and stale worker status are not treated as playing. Join uses the existing
specific-server flow and its capacity/account checks.

`/marketplace` lists free, approved T-shirt graphics. `/tshirts/upload` accepts
PNG/JPEG files up to 2 MB and 1024 by 1024 pixels, normalizes them to 256 by 256
RGBA PNGs, and queues them privately (five pending uploads per account).
`/manage/moderation` is HTTPS/admin-only, with pending/approved/rejected queues.
Pending and rejected images are visible only to the creator or HTTPS admin.
Approving publishes an image; rejecting removes its selections and refreshes
live server access files. Removing the creator account also retires its shirts.
Back up `data/tshirts` together with `data/grid.sqlite`. Pillow is required.

The optional final access-file field is a public HTTP T-shirt image URL or `-`.
All server backends apply a front-facing `_NotbloxTShirt` torso Decal and update
it after respawn or selection changes. Existing clients need no new binary.
The legacy `/downloads/notroblox.xdelta` URL remains a compatibility alias; the
current filename and download page use `notblox.xdelta`.
