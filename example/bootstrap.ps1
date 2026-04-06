
Write-Host -NoNewline "Bootstrapping ..."
& $env:CC -O0 -g --std=c++20 -Wno-braced-scalar-init now.cpp -o now.exe
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERR: Unable to compile bootstrap!"
} else {
    Write-Host "`rBootstrapping is done. Run now.exe to start."
}