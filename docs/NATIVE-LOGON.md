# Native logon: build and server setup

The DLL can speak Ascension's own logon handshake (X25519 key exchange, ChaCha20-encrypted hello,
HMAC-SHA256 proof) instead of stock SRP6. This is **off by default**. A default build authenticates with
stock SRP6 against any AzerothCore authserver and needs nothing on this page.

Turn it on only to test against a server that implements the same protocol. A native-logon client cannot
log in to a stock authserver. The reverse works: an authserver with custom auth enabled recognises the
custom hello per connection and still answers stock SRP6 clients.

## 1. Generate a key pair

The server holds an X25519 private key. The client is built with the matching public key (the original
DLL's "KC1" slot).

```powershell
.\tools\ascension-auth-keygen.ps1                        # new random pair: private + public, 64 hex each
.\tools\ascension-auth-keygen.ps1 -FromPrivate <64 hex>  # public key for an existing private key
```

The script self-tests its X25519 implementation against the RFC 7748 vectors before printing anything.

**Keep the private key secret.** It goes only into the server's config. Do not commit it, paste it into an
issue, or keep it in a synced folder. The public key is not secret.

The CMake default for `ASCENSION_NATIVE_LOGON_SERVER_KEY` is the public key used in the 2026-10-10 live
test. Its private key is not published, so **always pass your own public key** when you build for your
own server.

## 2. Build the client

```bat
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DASCENSION_NATIVE_LOGON=ON ^
      -DASCENSION_NATIVE_LOGON_SERVER_KEY=<your 64-hex public key> ^
      -S . -B build\nl
cmake --build build\nl
```

| Option | Default | Meaning |
|---|---|---|
| `ASCENSION_NATIVE_LOGON` | `OFF` | Compile in `src/Ascension/AscNativeLogon.cpp` and install its hooks at startup |
| `ASCENSION_NATIVE_LOGON_SERVER_KEY` | live-test key | The server's X25519 public key, 64 hex characters |

With the option on, the DLL replaces three client functions: the hello builder `0x8CCE00`, the session-key
copy `0x9A83E0`, and the proof check `0x9A88C0`. It also patches four length gates (`0x8CC7FA`, `0x8CC856`,
`0xDFC024`, `0xDFC034`) so the client reads the 32-byte proof.

Before installing anything, the module runs a crypto self-test. **If the self-test fails, it installs
nothing**, and the client falls back to stock SRP6.

Ship `build\nl\Extensions.dll` next to `Ascension.exe` as usual.

## 3. Set up the server

The server side is not in upstream `jealous-sound/azerothcore-wotlk-coa`. It lives on the
`feature/ascension-native-login` branch of
[CoinThrow/azerothcore-wotlk-coa](https://github.com/CoinThrow/azerothcore-wotlk-coa/tree/feature/ascension-native-login).
The relevant changes are in `src/server/apps/authserver/`: `Server/AscensionAuth.h`, `Server/AuthSession.cpp`
and `Main.cpp`.

The authserver reads `coa.conf` on that branch, so copy `coa.conf.dist` to `coa.conf` in the **authserver's**
config directory as well as the worldserver's. Edit the copy, never the `.dist`.

```ini
# Accept the original-client handshake. 0 = stock SRP6 only.
CoA.AscensionAuth = 1

# The server's X25519 private key (64 hex characters).
CoA.AscensionAuth.PrivateKey = "<your private key>"

# Optional: refuse custom auth unless this matches the public key derived from PrivateKey.
CoA.AscensionAuth.PublicKey = "<your public key>"
```

At startup, the authserver logs the public key derived from `PrivateKey`. Compare it with the key you built
the client with. A mismatch means the proof check fails on every login.

The worldserver needs the usual CoA settings for this client:

- `CoA.Enable = 1`
- `CoA.PlaintextWorldHeaders` as the README describes for your client
- `CoA.AllowRemoteClients = 1` for clients that do not connect from loopback

Accounts are ordinary AzerothCore accounts. The password inside the hello is checked against the account's
stored SRP6 verifier, so existing accounts work without changes.

### Required server behavior: persist the session key

After the custom proof succeeds, the authserver must store the handshake's 40-byte `key_session` as
`account.session_key`. The worldserver checks the client's `CMSG_AUTH_SESSION` digest against that value
and sets up its header cipher from it. Without it, every world connection fails the digest check right
after a successful login.

The `feature/ascension-native-login` branch does this in `AuthSession.cpp` (`HandleLogonProof`, Ascension
client branch). Any other server implementing this protocol must do the same.

## 4. Check that it works

Start the client with `-extlog 1` and read `Logs\Extensions.log`.

| Where | Expected line |
|---|---|
| client | `native-logon: proof check PASSED (HMAC-SHA256(key_derived, "OK"))` |
| authserver | `Ascension custom auth: proof replied, session authorised` |
| client | `COP_AUTHENTICATE ... AUTH_OK`, then `COP_GET_CHARACTERS ... result=TRUE` |

| Symptom | Likely cause |
|---|---|
| No `native-logon:` lines at all | The build did not enable `ASCENSION_NATIVE_LOGON`, or the self-test failed. Check the log's first lines. |
| Proof check fails | The client's public key does not match the server's private key (step 1) |
| Login succeeds, world connection drops at once | The server is not persisting `key_session` (step 3) |
| `Ascension custom auth hello rejected (CoA.AscensionAuth is disabled)` in the authserver log | `CoA.AscensionAuth = 1` is missing from the authserver's `coa.conf` |
| `Ascension custom auth: wrong password` | The account's password does not match its stored verifier, as with a normal failed login |
