#pragma once
// Ascension's own logon protocol, reconstructed (build option ASCENSION_NATIVE_LOGON, default OFF).
//
// The genuine DLL replaces the client's stock SRP6 logon with Ascension's own handshake by
// hot-patching three exe thunks (0x8CCE00 hello serialization, 0x9A83E0 key material, 0x9A88C0
// the server-proof check). This module installs a native reconstruction of those three pieces so
// the client speaks the same wire protocol as a server that implements it (CoA.AscensionAuth in
// the azerothcore-wotlk-coa fork). The wire format, constants and derivations are byte-exact
// copies of the values verified live during the 2026-10 native-login campaign (see the repo's
// tests and the reference implementations re/ascens_crypto.py, re/fake_auth8_pure.py,
// re/ascension_auth_probe.py and the server's AscensionAuth.h - keep them in step).
//
// With the option OFF (the default) nothing here is compiled in and the client authenticates
// with stock SRP6, exactly as before.
namespace AscNativeLogon
{
    // Installs the logon protocol: crypto self-test first (refuses to install on failure), then
    // the three thunk replacements and the four handshake length gates (0x14 -> 0x20). Safe to
    // call more than once. Called from DllMain when the build option is enabled.
    void Install();
}
