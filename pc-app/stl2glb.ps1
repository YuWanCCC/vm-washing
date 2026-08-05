# STL to GLB converter - single-pass streaming version
param(
    [string]$InputStl  = "$PSScriptRoot\LG Washing Machine.stl",
    [string]$OutputGlb = "$PSScriptRoot\LG Washing Machine.glb"
)

Write-Host "STL -> GLB Converter (single-pass)" -ForegroundColor Cyan

# --- Pass 1: bounds only ---
Write-Host "Pass 1: bounds..." -ForegroundColor Yellow
$fs = [System.IO.File]::OpenRead($InputStl)
$br = New-Object System.IO.BinaryReader($fs)
$br.ReadBytes(80) | Out-Null
$triCount = $br.ReadUInt32()
$vertCount = $triCount * 3
Write-Host "Triangles: $triCount, Vertices: $vertCount"

$minX=[float]::MaxValue;$minY=[float]::MaxValue;$minZ=[float]::MaxValue
$maxX=[float]::MinValue;$maxY=[float]::MinValue;$maxZ=[float]::MinValue

for ($t = 0; $t -lt $triCount; $t++) {
    $br.ReadBytes(12) | Out-Null
    for ($v = 0; $v -lt 3; $v++) {
        $x=$br.ReadSingle();$y=$br.ReadSingle();$z=$br.ReadSingle()
        if($x -lt $minX){$minX=$x};if($x -gt $maxX){$maxX=$x}
        if($y -lt $minY){$minY=$y};if($y -gt $maxY){$maxY=$y}
        if($z -lt $minZ){$minZ=$z};if($z -gt $maxZ){$maxZ=$z}
    }
    $br.ReadUInt16() | Out-Null
}
$br.Close(); $fs.Close()
Write-Host "Bounds: [$minX,$minY,$minZ] -> [$maxX,$maxY,$maxZ]" -ForegroundColor Green

# --- Sizes ---
$indexSize    = $vertCount * 4
$positionSize = $vertCount * 12
$normalSize   = $vertCount * 12
$totalBinSize = $indexSize + $positionSize + $normalSize
$binPadded = $totalBinSize + $(if($totalBinSize%4 -ne 0){4-$totalBinSize%4}else{0})

# --- JSON ---
$json = @"
{"asset":{"version":"2.0","generator":"STL2GLB-PS"},"scene":0,"scenes":[{"name":"Scene","nodes":[0]}],"nodes":[{"name":"WashingMachine","mesh":0}],"meshes":[{"name":"Mesh","primitives":[{"attributes":{"POSITION":1,"NORMAL":2},"indices":0,"mode":4}]}],"accessors":[{"bufferView":0,"componentType":5125,"count":$vertCount,"type":"SCALAR","max":[$($vertCount-1)],"min":[0]},{"bufferView":1,"componentType":5126,"count":$vertCount,"type":"VEC3","max":[$maxX,$maxY,$maxZ],"min":[$minX,$minY,$minZ]},{"bufferView":2,"componentType":5126,"count":$vertCount,"type":"VEC3","max":[1,1,1],"min":[-1,-1,-1]}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":$indexSize,"target":34963},{"buffer":0,"byteOffset":$indexSize,"byteLength":$positionSize,"byteStride":12,"target":34962},{"buffer":0,"byteOffset":$($indexSize+$positionSize),"byteLength":$normalSize,"byteStride":12,"target":34962}],"buffers":[{"byteLength":$totalBinSize}]}
"@
$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$jsonPadLen = if($jsonBytes.Length%4 -ne 0){4-$jsonBytes.Length%4}else{0}
$jsonPaddedLen = $jsonBytes.Length + $jsonPadLen
$totalLength = 12 + 8 + $jsonPaddedLen + 8 + $binPadded

# --- Pass 2: single read, write all 3 temp files, then assemble GLB ---
Write-Host "Pass 2: extracting data..." -ForegroundColor Yellow
$sw = [System.Diagnostics.Stopwatch]::StartNew()

$tmpIdx = [System.IO.Path]::GetTempFileName()
$tmpPos = [System.IO.Path]::GetTempFileName()
$tmpNrm = [System.IO.Path]::GetTempFileName()

$fsIdx = [System.IO.File]::Create($tmpIdx)
$fsPos = [System.IO.File]::Create($tmpPos)
$fsNrm = [System.IO.File]::Create($tmpNrm)
$bwIdx = New-Object System.IO.BinaryWriter($fsIdx)
$bwPos = New-Object System.IO.BinaryWriter($fsPos)
$bwNrm = New-Object System.IO.BinaryWriter($fsNrm)

$fs = [System.IO.File]::OpenRead($InputStl)
$br = New-Object System.IO.BinaryReader($fs)
$br.ReadBytes(80) | Out-Null
$br.ReadUInt32() | Out-Null

$vi = [uint32]0
for ($t = 0; $t -lt $triCount; $t++) {
    # Read face normal (12 bytes = 3 floats)
    $nx = $br.ReadSingle(); $ny = $br.ReadSingle(); $nz = $br.ReadSingle()

    # Write same normal for all 3 vertices
    $bwNrm.Write($nx); $bwNrm.Write($ny); $bwNrm.Write($nz)
    $bwNrm.Write($nx); $bwNrm.Write($ny); $bwNrm.Write($nz)
    $bwNrm.Write($nx); $bwNrm.Write($ny); $bwNrm.Write($nz)

    # Read 3 vertices and write positions + indices
    for ($v = 0; $v -lt 3; $v++) {
        $x = $br.ReadSingle(); $y = $br.ReadSingle(); $z = $br.ReadSingle()
        $bwPos.Write($x); $bwPos.Write($y); $bwPos.Write($z)
        $bwIdx.Write($vi)
        $vi++
    }

    $br.ReadUInt16() | Out-Null  # attribute byte count

    if (($t % 100000) -eq 0 -and $t -gt 0) {
        Write-Host "  $t / $triCount ..." -ForegroundColor DarkGray
    }
}

$br.Close(); $fs.Close()
$bwIdx.Close(); $fsIdx.Close()
$bwPos.Close(); $fsPos.Close()
$bwNrm.Close(); $fsNrm.Close()

Write-Host "  Extract done in $($sw.Elapsed.TotalSeconds.ToString('F1'))s" -ForegroundColor Green
Write-Host "  Index: $((Get-Item $tmpIdx).Length), Pos: $((Get-Item $tmpPos).Length), Nrm: $((Get-Item $tmpNrm).Length)"

# --- Assemble GLB ---
Write-Host "Assembling GLB..." -ForegroundColor Yellow

$outFs = [System.IO.File]::Create($OutputGlb)
$bw = New-Object System.IO.BinaryWriter($outFs)

# GLB header
$bw.Write([uint32]0x46546C67)
$bw.Write([uint32]2)
$bw.Write([uint32]$totalLength)

# JSON chunk
$bw.Write([uint32]$jsonPaddedLen)
$bw.Write([uint32]0x4E4F534A)
$bw.Write($jsonBytes)
for ($i = 0; $i -lt $jsonPadLen; $i++) { $bw.Write([byte]0x20) }

# BIN chunk header
$bw.Write([uint32]$binPadded)
$bw.Write([uint32]0x004E4942)

# Stream binary data from temp files
$bufSize = 256 * 1024
$buf = New-Object byte[] $bufSize

foreach ($tmpFile in @($tmpIdx, $tmpPos, $tmpNrm)) {
    $rfs = [System.IO.File]::OpenRead($tmpFile)
    $rbr = New-Object System.IO.BinaryReader($rfs)
    $remaining = $rfs.Length
    while ($remaining -gt 0) {
        $toRead = [int][Math]::Min($remaining, $bufSize)
        $read = $rbr.Read($buf, 0, $toRead)
        $bw.Write($buf, 0, $read)
        $remaining -= $read
    }
    $rbr.Close(); $rfs.Close()
}

# Pad
$binRem = $totalBinSize % 4
if ($binRem -ne 0) { for($i=0;$i -lt (4-$binRem);$i++){$bw.Write([byte]0)} }

$bw.Close(); $outFs.Close()

# Cleanup temp files
Remove-Item $tmpIdx -ErrorAction SilentlyContinue
Remove-Item $tmpPos -ErrorAction SilentlyContinue
Remove-Item $tmpNrm -ErrorAction SilentlyContinue

$outSize = (Get-Item $OutputGlb).Length
Write-Host "`nDone in $($sw.Elapsed.TotalSeconds.ToString('F1'))s!" -ForegroundColor Green
Write-Host "Output: $OutputGlb ($outSize bytes, $([math]::Round($outSize/1MB,1)) MB)" -ForegroundColor Cyan
