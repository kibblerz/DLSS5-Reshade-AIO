param([int]$LifetimeSeconds = 8)

$size = 320
$name = "Local\DLSS5_AIO_Telemetry_$PID"
$mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($name, $size)
$view = $mapping.CreateViewAccessor(0, $size, [IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)
$bytes = New-Object byte[] $size
[BitConverter]::GetBytes([uint32]0x4F494144).CopyTo($bytes, 0)
[BitConverter]::GetBytes([uint32]1).CopyTo($bytes, 4)
[BitConverter]::GetBytes([uint32]$size).CopyTo($bytes, 8)
[BitConverter]::GetBytes([uint32]$PID).CopyTo($bytes, 12)
[BitConverter]::GetBytes([int64]2).CopyTo($bytes, 16)
[BitConverter]::GetBytes([int64][Diagnostics.Stopwatch]::GetTimestamp()).CopyTo($bytes, 24)
[BitConverter]::GetBytes([int64][Diagnostics.Stopwatch]::Frequency).CopyTo($bytes, 32)
[BitConverter]::GetBytes([uint32]3).CopyTo($bytes, 44)
[BitConverter]::GetBytes([uint32]1920).CopyTo($bytes, 56)
[BitConverter]::GetBytes([uint32]1080).CopyTo($bytes, 60)
[BitConverter]::GetBytes([uint32]3840).CopyTo($bytes, 64)
[BitConverter]::GetBytes([uint32]2160).CopyTo($bytes, 68)
[BitConverter]::GetBytes([uint32]90).CopyTo($bytes, 72)
[BitConverter]::GetBytes([uint32]180).CopyTo($bytes, 76)
[BitConverter]::GetBytes([uint32]4000).CopyTo($bytes, 100)
[BitConverter]::GetBytes([uint32]1500).CopyTo($bytes, 104)
[BitConverter]::GetBytes([uint64]0x1111).CopyTo($bytes, 304)
[BitConverter]::GetBytes([uint64]0x2222).CopyTo($bytes, 312)
$view.WriteArray(0, $bytes, 0, $bytes.Length) | Out-Null

$timer = [Diagnostics.Stopwatch]::StartNew()
$sequence = [int64]2
while ($timer.Elapsed.TotalSeconds -lt $LifetimeSeconds) {
    $sequence++
    $view.Write(16, $sequence)
    $view.Write(24, [int64][Diagnostics.Stopwatch]::GetTimestamp())
    $sequence++
    $view.Write(16, $sequence)
    Start-Sleep -Milliseconds 50
}

$view.Dispose()
$mapping.Dispose()
