---
title: Configuration
order: 8.5
---

Settings that belong to a machine rather than a project live in the user config file, `spn/spn.toml` under the config directory:
- Linux and macOS: `$XDG_CONFIG_HOME`, or `~/.config` if it isn't set
- Windows: `%APPDATA%`

The file is optional. It holds [`[[index]]`](/docs/indexes#global-configuration) entries, `[[toolchain]]` entries, and `[net]`.

## [net]

```toml
[net]
timeout = 30
ca_bundle = "/etc/ssl/certs/ca-certificates.crt"
system_git = false
```

| Field | Type | Default | Meaning |
|---|---|---|---|
| `timeout` | integer | `30` | Seconds to wait to connect to a server, and for a stalled transfer to make progress, before giving up. It must be at least `1`; values above `65535` are treated as `65535` |
| `ca_bundle` | string | | Path to a PEM file of trusted root certificates |
| `system_git` | bool | `false` | Clone and fetch indexes and Git dependencies with the `git` on your `PATH` instead of the built in Git client |

These apply to Git, which `spn` uses for indexes and Git dependencies, and to downloads, which it uses for toolchains like Zig.

### Certificates

On Linux and macOS, `spn` verifies HTTPS servers against a single PEM file, chosen in this order:
1. `SSL_CERT_FILE`, if it's set
2. `ca_bundle`, if it's set
3. The first of these that exists: `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/ca-bundle.pem`, `/etc/pki/tls/cacert.pem`, `/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem`, `/etc/ssl/cert.pem`

Only the chosen file is trusted. A configured bundle replaces the system one rather than adding to it, so a file that holds nothing but a company's root certificate can't verify any other server; append the extra root to a copy of the system bundle and point `ca_bundle` at that.

On Windows, `spn` uses the system certificate store, and `ca_bundle` and `SSL_CERT_FILE` have no effect.

### Proxies

The built in Git client and downloads both read:
- `https_proxy` or `HTTPS_PROXY` for `https://` URLs
- `http_proxy` for `http://` URLs
- `no_proxy` or `NO_PROXY` for hosts to reach directly

The built in Git client also reads git's proxy settings (`http.proxy`), which take precedence over the environment, and accepts `HTTP_PROXY`. Downloads also fall back to `all_proxy` or `ALL_PROXY`.

### system_git

With `system_git = true`, `spn` runs `git clone` and `git fetch` for indexes and Git dependencies. It runs them with your environment, and doesn't pass them `timeout` or `ca_bundle`. Downloads are unaffected. `spn publish` always pushes with `git`, whatever this is set to.

## Environment variables

| Variable | Meaning |
|---|---|
| `SPN_CONFIG_DIR` | Replaces the config directory. `SPN_CONFIG_DIR=/tmp/a` reads `/tmp/a/spn/spn.toml` |
| `SPN_STORAGE_DIR` | Where `spn` keeps index checkouts, Git dependencies, and its build cache. Defaults to `spn` under `$XDG_DATA_HOME` (or `~/.local/share`) on Linux and macOS, and under `%LOCALAPPDATA%` on Windows |
| `SPN_TOOLCHAIN_DIR` | Where `spn` keeps the toolchains it downloads. Defaults to `cache/toolchain` under the storage directory |
| `SPN_MIRROR` | Base URL to download toolchains from. `spn` appends the archive's file name, tries only that URL, and still checks the archive against its checksum |
| `SPN_INDEX_REFRESH_SECONDS` | How long a synced Git index counts as fresh. Defaults to `600`. Once it's older, `spn add` fetches it again, and so does a build that can't resolve a dependency from the cached copy. Indexes pinned to a `rev` aren't refreshed, and `spn index sync` refreshes regardless |
| `SSL_CERT_FILE` | See [Certificates](/docs/configuration#certificates) |
| `https_proxy`, `http_proxy`, `no_proxy` | See [Proxies](/docs/configuration#proxies) |
