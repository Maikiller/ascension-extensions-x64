#Requires -Version 5.1
<#
.SYNOPSIS
    Generates an X25519 (RFC 7748) key pair as two 64-character hex strings.

.DESCRIPTION
    Prints the private key and its matching public key. By default a fresh
    random pair is generated; -FromPrivate derives the public key of an
    existing private key instead.

    The X25519 math (Montgomery ladder over 2^255 - 19) is implemented
    inline on System.Numerics.BigInteger - no OpenSSL or Python needed -
    and is self-tested before every run against the RFC 7748 section 6.1
    vectors and the pair this project's live login tests used, so a passing
    self-test ties the output to the implementation working in practice.

.PARAMETER FromPrivate
    Derive the public key of an existing 64-character hex private key
    instead of generating a new pair.

.NOTES
    In the Ascension client the public key is the value carried in the KC1
    slot. Both keys are 32 bytes (64 hex characters).

.EXAMPLE
    .\ascension-auth-keygen.ps1

.EXAMPLE
    .\ascension-auth-keygen.ps1 -FromPrivate 22dfa316a864ff398609b0b9ea1edec46ec6f7097a3d215dddfa1674e3d7f0f5
#>
[CmdletBinding()]
param(
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$FromPrivate
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$script:FieldP = [System.Numerics.BigInteger]::Pow(2, 255) - 19
$script:A24 = [System.Numerics.BigInteger]121665
$script:BasePoint = New-Object byte[] 32
$script:BasePoint[0] = 9

# ---- byte / hex helpers -----------------------------------------------------

function ConvertTo-Bytes([string]$Hex) {
    if ($Hex.Length % 2 -ne 0) { throw "hex string has odd length: $Hex" }
    $out = New-Object byte[] ($Hex.Length / 2)
    for ($i = 0; $i -lt $out.Length; $i++) {
        $out[$i] = [Convert]::ToByte($Hex.Substring($i * 2, 2), 16)
    }
    , $out
}

function ConvertTo-Hex([byte[]]$Bytes) {
    [System.BitConverter]::ToString($Bytes).Replace('-', '').ToLowerInvariant()
}

function Read-LeBig([byte[]]$Le) {
    $padded = New-Object byte[] ($Le.Length + 1)   # extra 0 byte keeps the value positive
    [Array]::Copy($Le, $padded, $Le.Length)
    [System.Numerics.BigInteger]::new($padded)
}

function Write-BigLe32([System.Numerics.BigInteger]$Value) {
    $raw = $Value.ToByteArray()                    # little-endian, two's complement
    $out = New-Object byte[] 32
    [Array]::Copy($raw, $out, [Math]::Min(32, $raw.Length))
    , $out
}

# ---- field arithmetic mod 2^255 - 19 ----------------------------------------

function ModAdd([System.Numerics.BigInteger]$A, [System.Numerics.BigInteger]$B) { ($A + $B) % $script:FieldP }

function ModSub([System.Numerics.BigInteger]$A, [System.Numerics.BigInteger]$B) {
    $r = $A - $B
    if ($r -lt 0) { $r += $script:FieldP }
    $r
}

function ModMul([System.Numerics.BigInteger]$A, [System.Numerics.BigInteger]$B) { ($A * $B) % $script:FieldP }

function ModPow([System.Numerics.BigInteger]$Base, [System.Numerics.BigInteger]$Exponent) {
    $r = [System.Numerics.BigInteger]1
    $b = $Base % $script:FieldP
    while ($Exponent -gt 0) {
        if (($Exponent % 2) -eq 1) { $r = ModMul $r $b }
        $b = ModMul $b $b
        $Exponent = [System.Numerics.BigInteger]::Divide($Exponent, 2)
    }
    $r
}

# ---- X25519 (RFC 7748 section 5) --------------------------------------------

function Invoke-X25519([byte[]]$Scalar, [byte[]]$U) {
    if ($Scalar.Length -ne 32 -or $U.Length -ne 32) { throw 'X25519 inputs must be 32 bytes' }

    $k = [byte[]]$Scalar.Clone()
    $k[0]  = $k[0] -band 0xF8                       # decodeScalar25519
    $k[31] = ($k[31] -band 0x7F) -bor 0x40

    $uB = [byte[]]$U.Clone()
    $uB[31] = $uB[31] -band 0x7F                    # decodeUCoordinate: ignore the top bit
    $x1 = Read-LeBig $uB

    $x2 = [System.Numerics.BigInteger]1
    $z2 = [System.Numerics.BigInteger]0
    $x3 = $x1
    $z3 = [System.Numerics.BigInteger]1
    $swap = $false

    for ($t = 254; $t -ge 0; $t--) {
        $kt = ((($k[$t -shr 3]) -shr ($t -band 7)) -band 1) -eq 1
        if ($swap -ne $kt) {
            $x2, $x3 = $x3, $x2
            $z2, $z3 = $z3, $z2
        }
        $swap = $kt

        $A  = ModAdd $x2 $z2
        $AA = ModMul $A $A
        $B  = ModSub $x2 $z2
        $BB = ModMul $B $B
        $E  = ModSub $AA $BB
        $C  = ModAdd $x3 $z3
        $D  = ModSub $x3 $z3
        $DA = ModMul $D $A
        $CB = ModMul $C $B
        $s  = ModAdd $DA $CB
        $x3 = ModMul $s $s
        $d  = ModSub $DA $CB
        $z3 = ModMul $x1 (ModMul $d $d)
        $x2 = ModMul $AA $BB
        $z2 = ModMul $E (ModAdd $AA (ModMul $script:A24 $E))
    }
    if ($swap) {
        $x2, $x3 = $x3, $x2
        $z2, $z3 = $z3, $z2
    }

    Write-BigLe32 (ModMul $x2 (ModPow $z2 ($script:FieldP - 2)))
}

# ---- self-test --------------------------------------------------------------

function Assert-HexEqual([string]$Name, [byte[]]$Actual, [string]$ExpectedHex) {
    $got = ConvertTo-Hex $Actual
    if ($got -ne $ExpectedHex) {
        throw "self-test failed ($Name):`n  got      $got`n  expected $ExpectedHex"
    }
}

function Test-X25519 {
    # RFC 7748 section 5.2: function vectors 1 and 2.
    Assert-HexEqual 'rfc-7748-1' (Invoke-X25519 (ConvertTo-Bytes 'a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4') (ConvertTo-Bytes 'e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c')) 'c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552'
    Assert-HexEqual 'rfc-7748-2' (Invoke-X25519 (ConvertTo-Bytes '4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d') (ConvertTo-Bytes 'e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493')) '95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957'

    # RFC 7748 section 6.1: Diffie-Hellman vectors (key pairs and both shared secrets).
    $alicePriv = ConvertTo-Bytes '77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a'
    $alicePub = ConvertTo-Bytes '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a'
    $bobPriv = ConvertTo-Bytes '5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb'
    $bobPub = ConvertTo-Bytes 'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f'
    Assert-HexEqual 'rfc-7748-alice' (Invoke-X25519 $alicePriv $script:BasePoint) '8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a'
    Assert-HexEqual 'rfc-7748-bob' (Invoke-X25519 $bobPriv $script:BasePoint) 'de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f'
    Assert-HexEqual 'rfc-7748-shared' (Invoke-X25519 $alicePriv $bobPub) '4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742'
    Assert-HexEqual 'rfc-7748-symmetric' (Invoke-X25519 $bobPriv $alicePub) '4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742'

    # The pair this project's live login tests used (handshakes passed in both roles with
    # it) - ties this implementation to the one working in practice.
    Assert-HexEqual 'project-live-pair' (Invoke-X25519 (ConvertTo-Bytes '22dfa316a864ff398609b0b9ea1edec46ec6f7097a3d215dddfa1674e3d7f0f5') $script:BasePoint) '1a03d4c71f289f39046fad519f8af3e92b793709a8a340e492b4ff5ff55e1537'
}

# ---- main -------------------------------------------------------------------

Write-Host 'X25519 self-test (RFC 7748 vectors + the project live-test pair)...'
Test-X25519
Write-Host 'self-test: PASS' -ForegroundColor Green

if ($FromPrivate) {
    $priv = ConvertTo-Bytes $FromPrivate
    Write-Host 'mode: derive the public key from the supplied private key'
}
else {
    $priv = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($priv)
    Write-Host 'mode: generate a new key pair'
}

$pub = Invoke-X25519 $priv $script:BasePoint
$privHex = ConvertTo-Hex $priv
$pubHex = ConvertTo-Hex $pub

Write-Host ''
Write-Host 'Private key:'
Write-Host "  $privHex"
Write-Host 'Public key:'
Write-Host "  $pubHex"
Write-Host ''
Write-Host 'Notes:'
Write-Host '  - X25519 (RFC 7748); both values are 32 bytes / 64 hex characters.'
Write-Host '  - Keep the private key secret; the public key is the shareable one.'
