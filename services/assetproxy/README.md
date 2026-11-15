# Asset archive

Public HTTP endpoint: `http://assetproxy.blox.ralsei.tech/<numeric-id>`.
On a cache miss it downloads the exact bytes from
`https://assetdelivery.roblox.com/v1/asset/?id=<numeric-id>` (following only
HTTPS Roblox/CDN redirects). Successful downloads are stored permanently,
without expiration, under `/data/assets/<id>/{body,meta.json}`. Back up this
directory. A cached response never contacts Roblox, even after service restart.
Unavailable, empty, truncated, oversized, HTML/error responses are not stored;
the next request can retry. The first successful asset version is retained.
This cannot recover assets Roblox has already made unavailable.

Downloads first use the legacy client's `User-Agent: Roblox` without cookies.
Only if Roblox rejects access (401/403/409) is a configured account cookie tried.
Public assets therefore do not depend on the account or its cookie.
For authenticated downloads, set `ASSET_PROXY_COOKIE_FILE` to a private Netscape
cookies.txt file containing the owner's `.ROBLOSECURITY` cookie. Only that cookie
from roblox.com is used, only on the initial HTTPS assetdelivery request; it is
stripped on redirects and never sent to clients. No browser cookies from public
requests are forwarded. An expired/revoked cookie needs replacement, but cached
assets continue working without it. Keep the secret outside source/downloads.

Import a previously downloaded asset with `python import_asset.py ID FILE` in
the proxy's environment. Existing archived files are never overwritten.

The HTTPS owner page `/manage/assets` allows replacing the cookies.txt export
and testing downloads. It defaults to teapot ID `1029523`, displays saved/new
download status and byte size, and never displays cookie values. Invalid
uploads leave the previous cookie intact. The grid container requires a writable
directory mount at `/assetproxy-secrets`; the proxy mounts the same directory
read-only at `/secrets`.

Run using the grid image's Flask/gunicorn environment, a separate read-only
source mount and persistent data volume. Bind gunicorn to localhost port 4381
and install nginx.conf. No HTTPS redirect is used.

Client Tools → Use Asset Proxy defaults off and saves its preference. Known
Roblox asset URLs are rewritten for HTTP GETs only; authentication, uploads,
local/bundled/embedded assets and unrelated websites are left alone. Disable
the option for direct original-URL requests; already-loaded engine assets stay
in memory until the place/client reloads. The Wine thumbnail worker defaults on
independently of the interactive client's saved preference.

Place create/edit pages have an Enabled / User choice / Disabled policy. New
rooms snapshot that policy and pass it to joining updated clients before they
connect; a forced policy locks the Tools toggle without overwriting the saved
preference. Closing the owning game clears its override. Older clients without
the policy API can still join but do not enforce it. Policy changes apply to new
rooms and regenerate the thumbnail; running rooms keep their original policy.
Servers and thumbnails use the proxy for Enabled and User choice, not Disabled.
