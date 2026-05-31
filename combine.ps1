param([string]$boot, [string]$kernel, [string]$output)
$FLOPPY_SIZE = 1474560
$bootData = [System.IO.File]::ReadAllBytes($boot)
$kernelData = [System.IO.File]::ReadAllBytes($kernel)
$image = $bootData + $kernelData
if ($image.Length -lt $FLOPPY_SIZE) {
    $image = $image + @([byte]0) * ($FLOPPY_SIZE - $image.Length)
}
[System.IO.File]::WriteAllBytes($output, $image)
