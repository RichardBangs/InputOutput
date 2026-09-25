param([switch]$Bootstrap, [switch]$Test, [switch]$Package, [string]$Destination = 'dist')
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$toolchainRoot = Join-Path $projectRoot '.tools\llvm-mingw-20260616-ucrt-x86_64'
$compilerPath = Join-Path $toolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compilerPath)) {
    if (-not $Bootstrap) { throw 'Build tools missing. Run .\build.ps1 -Bootstrap -Test once.' }
    $toolDirectory = Join-Path $projectRoot '.tools'
    New-Item -ItemType Directory -Force -Path $toolDirectory | Out-Null
    $archivePath = Join-Path $toolDirectory 'toolchain.zip'
    Invoke-WebRequest -Uri 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip' -OutFile $archivePath
    $expectedHash = 'b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35'
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLower() -ne $expectedHash) { throw 'Build-tool download failed its checksum check.' }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $toolDirectory -Force
}
$buildDirectory = Join-Path $projectRoot 'build'
$outputDirectory = Join-Path $projectRoot $Destination
New-Item -ItemType Directory -Force -Path $buildDirectory,$outputDirectory | Out-Null
$resourcePath = Join-Path $buildDirectory 'app.res.o'
$resourceCompiler = Join-Path $toolchainRoot 'bin\x86_64-w64-mingw32-windres.exe'
Push-Location (Join-Path $projectRoot 'assets')
try {
    & $resourceCompiler 'app.rc' '-O' 'coff' '-o' $resourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Resource build failed.' }
} finally { Pop-Location }
$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -Filter '*.cpp' | ForEach-Object FullName
$applicationPath = Join-Path $outputDirectory 'InputOutput.exe'
& $compilerPath '-std=c++20' '-O2' '-g' '-static' '-municode' '-mwindows' '-DUNICODE' '-D_UNICODE' '-D_WIN32_WINNT=0x0A00' '-Wall' '-Wextra' '-Wpedantic' '-Wno-missing-field-initializers' @sourceFiles $resourcePath '-o' $applicationPath '-luser32' '-lshell32' '-lole32' '-luuid' '-lpropsys' '-ladvapi32' '-lcomctl32' '-ldwmapi' '-lgdi32' '-lpsapi' '-luxtheme'
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
& (Join-Path $toolchainRoot 'bin\llvm-objcopy.exe') '--only-keep-debug' $applicationPath (Join-Path $buildDirectory 'InputOutput.debug')
& (Join-Path $toolchainRoot 'bin\llvm-strip.exe') '--strip-debug' $applicationPath
if ($Test) {
    $testDirectory = Join-Path $buildDirectory 'self-test'
    $process = Start-Process -FilePath $applicationPath -ArgumentList @('--self-test','--data-dir',('"' + $testDirectory + '"')) -PassThru -Wait -WindowStyle Hidden
    Get-Content -LiteralPath (Join-Path $testDirectory 'self-test.txt')
    if ($process.ExitCode -ne 0) { throw 'Self-tests failed.' }
}
if ($Package) {
    $licenseDirectory = Join-Path $outputDirectory 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseDirectory | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'licenses') -File | Copy-Item -Destination $licenseDirectory -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'USAGE.md'),(Join-Path $projectRoot 'TESTING.md'),(Join-Path $projectRoot 'VERIFICATION.md'),(Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $outputDirectory -Force
    $screenshotDirectory = Join-Path $outputDirectory 'assets\screenshots'
    New-Item -ItemType Directory -Force -Path $screenshotDirectory | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'assets\screenshots') -Filter '*.png' | Copy-Item -Destination $screenshotDirectory -Force
    Compress-Archive -LiteralPath $applicationPath,(Join-Path $outputDirectory 'README.md'),(Join-Path $outputDirectory 'USAGE.md'),(Join-Path $outputDirectory 'TESTING.md'),(Join-Path $outputDirectory 'VERIFICATION.md'),(Join-Path $outputDirectory 'LICENSE'),(Join-Path $outputDirectory 'THIRD_PARTY_NOTICES.md'),(Join-Path $outputDirectory 'assets'),$licenseDirectory -DestinationPath (Join-Path $outputDirectory 'InputOutput-0.1.0-win64.zip') -Force
}
Get-Item -LiteralPath $applicationPath | Select-Object FullName,Length,LastWriteTime
