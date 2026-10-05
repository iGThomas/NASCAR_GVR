# ============================================================
# Decrypt-GvrEnc.ps1
#
# Decrypts GlobalVR GvrPlus ".enc" schema files (Base64 + AES-128-CBC,
# PKCS#7). Same key/IV as the NFSU cabinet -- GlobalVR reused the
# GvrPlus crypto across titles, verified against NASCAR 2008 media.
#
# The key and IV are derived by the same rolling-XOR the GvrPeUtil
# CryptoUtils code uses (see GIT\decrypt_gvr.py in the parent project).
#
#   .\Decrypt-GvrEnc.ps1 NASCARcabinet.enc NASCARcabinet.txt
#   .\Decrypt-GvrEnc.ps1 -InputPath .\schema -OutputPath .\out   # whole folder
# ============================================================

param(
    [Parameter(Mandatory = $true, Position = 0)][string]$InputPath,
    [Parameter(Mandatory = $true, Position = 1)][string]$OutputPath
)

$ErrorActionPreference = "Stop"

function Get-RollingKey([int[]]$Seed, [int]$Start) {
    $b = $Start
    $out = New-Object byte[] 16
    for ($i = 0; $i -lt 16; $i++) {
        $b = ($b -bxor $Seed[$i]) -band 0xFF
        $out[$i] = [byte]$b
    }
    return $out
}

$key = Get-RollingKey @(61, 221, 17, 85, 239, 86, 26, 52, 120, 171, 55, 147, 35, 124, 101, 214) 156
$iv = Get-RollingKey @(52, 69, 116, 204, 177, 79, 57, 146, 255, 239, 184, 172, 101, 16, 144, 169) 93

function Convert-OneFile($in, $out) {
    $b64 = (Get-Content -LiteralPath $in -Raw).Trim()
    $cipher = [Convert]::FromBase64String($b64)

    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
    $aes.KeySize = 128
    $aes.Key = $key
    $aes.IV = $iv
    try {
        $dec = $aes.CreateDecryptor()
        $plain = $dec.TransformFinalBlock($cipher, 0, $cipher.Length)
    }
    finally { $aes.Dispose() }

    # The plaintext is single-byte (latin-1) SQL/XML text.
    $text = [System.Text.Encoding]::GetEncoding("iso-8859-1").GetString($plain)
    Set-Content -LiteralPath $out -Value $text -Encoding UTF8
    "{0} -> {1} ({2:N0} bytes)" -f (Split-Path -Leaf $in), (Split-Path -Leaf $out), $plain.Length
}

if (Test-Path -LiteralPath $InputPath -PathType Container) {
    if (!(Test-Path $OutputPath)) { New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null }
    Get-ChildItem -LiteralPath $InputPath -Filter *.enc | ForEach-Object {
        Convert-OneFile $_.FullName (Join-Path $OutputPath ($_.BaseName + ".txt"))
    }
}
else {
    Convert-OneFile $InputPath $OutputPath
}
