<#
.SYNOPSIS
  Shift-JIS(CP932) のソースを UTF-8 with BOM に変換する。
.DESCRIPTION
  既に UTF-8 のファイル、純 ASCII のファイル、サードパーティは触らない。
  CP932 として往復変換できないファイルは安全のためスキップして報告する。
.EXAMPLE
  pwsh -File tools\Convert-Utf8Bom.ps1           # ドライラン（何も書き換えない）
  pwsh -File tools\Convert-Utf8Bom.ps1 -Apply    # 実変換
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

# 変換対象の拡張子
# 変換対象の拡張子
# 注意: .hlsl/.hlsli は含めない。fxc/D3DCompileFromFile が UTF-8 BOM を
#       受け付けず error X3000 になるため、BOM なし UTF-8 で運用する。
$includeExt = @('.cpp', '.hpp', '.h', '.c', '.inl')

# 触らないディレクトリ（サードパーティ・生成物）
$excludeDirs = @('.git', 'x64', 'Debug', 'Release', 'packages', 'lib', 'Build', 'Bin',
                 'imgui-master', 'assimp', 'DirectXTex', 'PhysicsX')

# 触らないファイル（ベンダーの単体ヘッダ。無意味な巨大差分を避ける）
$excludeFiles = @('json.hpp', 'cr.h', 'd3dx12.h')

$cp932      = [Text.Encoding]::GetEncoding(932)
$utf8Bom    = New-Object Text.UTF8Encoding($true)          # BOM あり
$utf8Strict = New-Object Text.UTF8Encoding($false, $true)  # 不正バイトで例外

function Test-BytesEqual([byte[]]$a, [byte[]]$b) {
    if ($a.Length -ne $b.Length) { return $false }
    for ($i = 0; $i -lt $a.Length; $i++) { if ($a[$i] -ne $b[$i]) { return $false } }
    return $true
}

$files = Get-ChildItem -LiteralPath $Root -Recurse -File |
    Where-Object { $includeExt -contains $_.Extension.ToLower() } |
    Where-Object { $excludeFiles -notcontains $_.Name } |
    Where-Object {
        $rel   = $_.FullName.Substring($Root.Length).TrimStart('\')
        $parts = $rel -split '\\'
        -not ($parts | Where-Object { $excludeDirs -contains $_ })
    }

$report = foreach ($f in $files) {
    $rel   = $f.FullName.Substring($Root.Length).TrimStart('\')
    $bytes = [IO.File]::ReadAllBytes($f.FullName)

    if ($bytes.Length -eq 0) {
        [pscustomobject]@{ Status = 'skip'; Reason = 'empty'; File = $rel }
        continue
    }

    # 既に UTF-8 BOM 付き
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        [pscustomobject]@{ Status = 'skip'; Reason = 'already UTF-8 BOM'; File = $rel }
        continue
    }

    # UTF-16 は対象外（手動対応）
    if ($bytes.Length -ge 2 -and
        (($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) -or ($bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF))) {
        [pscustomobject]@{ Status = 'skip'; Reason = 'UTF-16 (手動で確認)'; File = $rel }
        continue
    }

    # 非 ASCII バイトが無ければ変換不要
    $hasHighByte = $false
    foreach ($b in $bytes) { if ($b -gt 0x7F) { $hasHighByte = $true; break } }
    if (-not $hasHighByte) {
        [pscustomobject]@{ Status = 'skip'; Reason = 'pure ASCII'; File = $rel }
        continue
    }

    # UTF-8 として妥当か判定
    $isUtf8 = $true
    try { [void]$utf8Strict.GetString($bytes) } catch { $isUtf8 = $false }

    if ($isUtf8) {
        # BOM なし UTF-8 → BOM を付けるだけ
        $text   = $utf8Strict.GetString($bytes)
        $reason = 'UTF-8 no BOM -> add BOM'
    }
    else {
        # CP932 とみなしてデコード、往復検証
        $text = $cp932.GetString($bytes)
        $back = $cp932.GetBytes($text)
        if (-not (Test-BytesEqual $bytes $back)) {
            [pscustomobject]@{ Status = 'SKIP!'; Reason = 'CP932 往復不一致（要手動確認）'; File = $rel }
            continue
        }
        $reason = 'CP932 -> UTF-8 BOM'
    }

    if ($Apply) {
        [IO.File]::WriteAllText($f.FullName, $text, $utf8Bom)
        [pscustomobject]@{ Status = 'converted'; Reason = $reason; File = $rel }
    }
    else {
        [pscustomobject]@{ Status = 'would convert'; Reason = $reason; File = $rel }
    }
}

$report | Sort-Object Status, File | Format-Table -AutoSize
""
$report | Group-Object Status | ForEach-Object { "{0,-14} {1}" -f $_.Name, $_.Count }
if (-not $Apply) { ""; "ドライランです。実行するには -Apply を付けてください。" }
