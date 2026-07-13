# authd auth service — integration notes

authd is now the **auth authority**: it verifies local credentials (from `local_users`)
and drives the **Okta OIDC** transaction (state/nonce/PKCE, `/token` exchange, id_token
RS256+JWKS verification). mgmtd keeps owning the **session cookie** — authd only returns
an authenticated identity.

Status: **authd side implemented and compiles** (`make pz-authd` is green). mgmtd relay
and two integration points below are still open.

## Files added (authd)
- `service/auth/AuthEvent.{h,cpp}`      — Auth domain event (carries the IPC request)
- `service/auth/AuthAction.{h,cpp}`     — response action (echoes src + seqNo)
- `service/auth/AuthService.{h,cpp}`    — local verify + Okta orchestration + Tx
- `service/auth/OktaClient.{h,cpp}`     — OIDC: authorize URL, code exchange, id_token verify
- `io/HttpsClient.{h,cpp}`              — Boost.Beast/OpenSSL client (copy of scand's; dedupe later)

Wiring edits: `IpcProtocol.{h,cpp}` (new cmds), `AuthdEvent.h`/`AuthdAction.h` (Auth domain),
`AuthdEventFactory.cpp` (cmd→event), `AuthdServiceManager.{h,cpp}` (register + configure),
`AuthdCore.cpp` (configure hook), `authd/CMakeLists.txt` (Boost/OpenSSL).

## IPC contract (mgmtd ⇄ authd)
| cmd | dir | request JSON | response JSON |
|-----|-----|--------------|---------------|
| `AuthLoginRequest`/`Response` (116/117) | mgmtd→authd→mgmtd | `{"username","password"}` | `{"success","username","must_change","error"}` |
| `AuthOidcStartRequest`/`Response` (118/119) | mgmtd→authd→mgmtd | `{}` | `{"success","authorize_url","state","error"}` |
| `AuthOidcCallbackRequest`/`Response` (120/121) | mgmtd→authd→mgmtd | `{"code","state"}` | `{"success","username","error"}` |

Every response echoes the request `seqNo` (correlation), `Response` flag set, `src=Authd`.

## Config keys (authd config JSON: `service.auth.oidc`)
```json
{ "service": { "auth": { "oidc": {
  "enabled": true,
  "issuer": "https://<tenant>.okta.com/oauth2/default",
  "client_id": "0oaXXXX",
  "client_secret": "XXXX",
  "redirect_uri": "https://<appliance>/authorization-code/callback",
  "scopes": "openid email profile",
  "verify_tls": true,
  "timeout_ms": 5000,
  "txn_ttl_sec": 300
}}}}
```
Absent/`enabled:false` → OIDC off, only local login works.

## Method select + SAML 2.0
`service.auth.method` = `"oidc"` (default) | `"saml"`. Both are compiled in; the selected
one is used, the other's IPC requests return `"... not selected"`.

SAML IPC: `AuthSamlStartRequest`/`Response` (122/123) → `{redirect_url,request_id}`;
`AuthSamlAcsRequest`/`Response` (124/125) `{saml_response}` → `{success,username,error}`.

SAML config (`service.auth.saml`):
```json
{ "service": { "auth": {
  "method": "saml",
  "saml": {
    "enabled": true,
    "idp_sso_url":  "https://<tenant>.okta.com/app/<id>/sso/saml",
    "idp_entity_id":"http://www.okta.com/<id>",
    "idp_cert_pem": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----",
    "sp_entity_id": "https://<appliance>/saml/metadata",
    "acs_url":      "https://<appliance>/api/auth/saml/acs",
    "admin_group":  "pretzel-admins",
    "groups_attr":  "groups",
    "email_attr":   "email",
    "clock_skew_sec": 120
  }
}}}
```
Verification is signature-first (xmlsec, IdP cert) then Issuer/Audience/time window, and the
admin-group allowlist. **SECURITY REVIEW REQUIRED**: test against the real tenant and XML
signature-wrapping (XSW) vectors before production; attributes are read only from the signed
assertion node, but this needs adversarial testing.

## OPEN #1 — authd must initialise the DB connection
`AuthService::verifyLocal` calls `pz::db::Database::instance().queryRows(...)`. authd does
not open a DB connection today. Add the same init mgmtd does (connection params from config)
in `AuthdCore::onInit` before the loop starts. Until then, local verify fails closed.
Reference: how `MgmtdCore` initialises `pz::db::Database`.

## DECISION — Option A chosen (Okta via authd, local stays in mgmtd)

Local password verification stays in mgmtd (unchanged). authd's local-verify path
(`AuthLoginRequest`) is left in place, dormant, ready for a future Option B. **authd needs
no DB connection under A** (Okta identity comes from the id_token), so OPEN #1 is deferred.

Wrinkle found while scoping A: the **callback** (`/authorization-code/callback`) still needs
authd's reply (token exchange + id_token verify live in authd with the client secret), and the
HTTP handler can't block. Clean fit without threading changes — reuse mgmtd's existing
fire-and-forget + poll pattern **only for the callback**:

1. `GET /api/auth/okta/login` → mgmtd sends `AuthOidcStartRequest`; on the `AuthOidcStartResponse`
   (mgmtd RxRouter) it stashes nothing user-visible — instead, to avoid a start round-trip too,
   authd returns the full `authorize_url`. Simplest: mgmtd holds the browser with a tiny
   "redirecting…" page that polls for the URL, OR (preferred) mgmtd builds the authorize URL
   locally from config (all params are non-secret) and only delegates the callback. **Pick at
   implementation time**; local-URL-build removes the start round-trip entirely.
2. `GET /authorization-code/callback?code&state` → mgmtd sends `AuthOidcCallbackRequest`
   (fire-and-forget) and returns a "signing in…" page that polls `/api/auth/okta/result?state=`.
   mgmtd RxRouter stores the `AuthOidcCallbackResponse` in a small `state→result` map; the poll
   then creates the **session cookie** (existing mgmtd session logic) and redirects to the app.

## OPEN #2 (superseded by DECISION above) — original option list
mgmtd's HTTP handler runs on the **same thread as the IPC poll** (`io_context.poll()`) and
**must not block** (see `mgmtd/http/HttpRouter.cpp:338`). So `/api/login` cannot synchronously
wait for `AuthLoginResponse` inline. Pick one:

- **A. Local stays in mgmtd, only Okta goes to authd (smallest change).**
  Okta is already async (browser 302 + separate `/authorization-code/callback` request), so no
  inline blocking is needed. `/api/auth/okta/login` → send `AuthOidcStartRequest`, and on the
  RxRouter response, 302 to `authorize_url`. Callback → `AuthOidcCallbackRequest`, response →
  create session cookie. Local password path unchanged. **Recommended first step.**

- **B. Route local login through authd too, via a blocking request helper.**
  Requires moving the IPC poll (or the HTTP server) onto its own thread so a CV-based
  `sendAndWait(seqNo, timeout)` can block the HTTP handler without starving the IPC read.
  Larger change; do this once A is proven.

- **C. Async login with a ticket** (`/api/login` returns pending → browser polls status),
  mirroring the settings-commit flow. Works single-threaded but worsens login UX.

## Security notes
- id_token is verified **signature-first, fail-closed**: RS256 against the JWKS key by `kid`,
  then `iss`/`aud`/`exp`/`nonce`. No unsigned-token path.
- TODO(perf): `verifySignatureRs256` fetches JWKS per login — add a `kid`→key cache (short TTL).
- Keep `verify_tls: true` for Okta (public CA). Test end-to-end against a real tenant.
- `client_secret` lives only in authd config + authd memory; it never reaches mgmtd/browser.
