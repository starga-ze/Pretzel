# secret/

Holds the credential-store master key (`credentials.key`), which encrypts device API keys
before they are written to `api_key_state`.

The key is **generated per install** by `script/start.py` (`ensure_credentials_key`) and is
gitignored, exactly as `cert/server.key` is. It must never be committed: the repository is
shared, and anyone holding both the key and a database copy can decrypt every customer's API
credentials.

`./pretzel start` deploys it to `/etc/pretzel/credentials.key` with mode 0600.

Losing the file is recoverable — the stored keys become undecryptable, and each API Key is
re-issued by running its Key Gen Test again.
