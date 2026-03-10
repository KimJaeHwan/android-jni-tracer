# Android Build Script for Windows
# Builds JNI harness for Android devices using NDK

param(
    [string]$NdkVersion = "",
    [int]$ApiLevel = 21,
    [ValidateSet("arm64", "x86_64")]
    [string]$Architecture = "arm64"
)

# Color output functions
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Write-Warning-Custom {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

# Banner
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Android JNI Harness Builder ($Architecture)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Find NDK installation
$AndroidSdkPath = "$env:LOCALAPPDATA\Android\Sdk"
$NdkBasePath = "$AndroidSdkPath\ndk"

if (-not (Test-Path $NdkBasePath)) {
    Write-Error-Custom "Android NDK not found at: $NdkBasePath"
    Write-Host ""
    Write-Host "Please install NDK from Android Studio:"
    Write-Host "  Tools -> SDK Manager -> SDK Tools -> NDK (Side by side)"
    exit 1
}

# Auto-detect NDK version if not specified
if ([string]::IsNullOrEmpty($NdkVersion)) {
    $AvailableVersions = Get-ChildItem $NdkBasePath | Sort-Object Name -Descending
    if ($AvailableVersions.Count -eq 0) {
        Write-Error-Custom "No NDK versions found in $NdkBasePath"
        exit 1
    }
    $NdkVersion = $AvailableVersions[0].Name
    Write-Info "Auto-detected NDK version: $NdkVersion"
} else {
    Write-Info "Using NDK version: $NdkVersion"
}

$NdkRoot = "$NdkBasePath\$NdkVersion"
if (-not (Test-Path $NdkRoot)) {
    Write-Error-Custom "NDK version $NdkVersion not found at: $NdkRoot"
    exit 1
}

# Setup toolchain paths based on architecture
$ToolchainRoot = "$NdkRoot\toolchains\llvm\prebuilt\windows-x86_64"
$SysrootPath = "$ToolchainRoot\sysroot"

if ($Architecture -eq "arm64") {
    $CompilerPrefix = "aarch64-linux-android"
    $OutputSuffix = "arm64_android"
} else {
    $CompilerPrefix = "x86_64-linux-android"
    $OutputSuffix = "x86_64_android"
}

$CompilerBin = "$ToolchainRoot\bin\$CompilerPrefix$ApiLevel-clang.cmd"

if (-not (Test-Path $CompilerBin)) {
    Write-Error-Custom "Compiler not found: $CompilerBin"
    Write-Warning-Custom "Available API levels:"
    Get-ChildItem "$ToolchainRoot\bin\$CompilerPrefix*-clang.cmd" | ForEach-Object {
        $Name = $_.Name -replace "$CompilerPrefix(\d+)-clang\.cmd", '$1'
        Write-Host "  API Level $Name"
    }
    exit 1
}

Write-Success "NDK found at: $NdkRoot"
Write-Info "Target Architecture: $Architecture"
Write-Info "Target API Level: $ApiLevel"
Write-Info "Compiler: $CompilerPrefix$ApiLevel-clang"

# Create output directories
$BuildDir = "build"
$LogDir = "logs"
$TargetDir = "target"

@($BuildDir, $LogDir, $TargetDir) | ForEach-Object {
    if (-not (Test-Path $_)) {
        New-Item -ItemType Directory -Path $_ -Force | Out-Null
        Write-Info "Created directory: $_"
    }
}

# Source files
$Sources = @(
    "src\main.c",
    "src\fake_jni.c",
    "src\jni_logger.c",
    "src\json_logger.c"
)

# Compiler flags
$CFlags = @(
    "-Wall",
    "-Wextra",
    "-g",
    "-O2",
    "-Iinclude",
    "-Isrc",
    "--sysroot=$SysrootPath",
    "-fPIC",
    "-DANDROID"
)

$LdFlags = @(
    "-ldl",
    "-llog"
)

$OutputBinary = "$BuildDir\jni_harness_$OutputSuffix"

Write-Host ""
Write-Info "Starting compilation..."
Write-Host ""

# Compile each source file
$ObjectFiles = @()
$CompilationSuccess = $true

foreach ($Source in $Sources) {
    if (-not (Test-Path $Source)) {
        Write-Error-Custom "Source file not found: $Source"
        $CompilationSuccess = $false
        continue
    }

    $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($Source)
    $ObjectFile = "$BuildDir\$BaseName.o"
    $ObjectFiles += $ObjectFile

    Write-Host "  Compiling: $Source" -ForegroundColor Yellow
    
    $CompileArgs = $CFlags + @("-c", $Source, "-o", $ObjectFile)
    $CompileCommand = "& `"$CompilerBin`" $CompileArgs"
    
    try {
        $process = Start-Process -FilePath $CompilerBin -ArgumentList $CompileArgs -NoNewWindow -Wait -PassThru -RedirectStandardError "$BuildDir\error_$BaseName.txt"
        
        if ($process.ExitCode -ne 0) {
            Write-Error-Custom "Failed to compile $Source"
            if (Test-Path "$BuildDir\error_$BaseName.txt") {
                Write-Host (Get-Content "$BuildDir\error_$BaseName.txt" -Raw) -ForegroundColor Red
            }
            $CompilationSuccess = $false
        } else {
            Write-Host "    -> $ObjectFile" -ForegroundColor Green
        }
    } catch {
        Write-Error-Custom "Exception while compiling $Source $_"
        $CompilationSuccess = $false
    }
}

if (-not $CompilationSuccess) {
    Write-Host ""
    Write-Error-Custom "Compilation failed!"
    exit 1
}

# Link all object files
Write-Host ""
Write-Info "Linking executable..."

$LinkArgs = $ObjectFiles + $LdFlags + @("-o", $OutputBinary)

try {
    $process = Start-Process -FilePath $CompilerBin -ArgumentList $LinkArgs -NoNewWindow -Wait -PassThru -RedirectStandardError "$BuildDir\error_link.txt"
    
    if ($process.ExitCode -ne 0) {
        Write-Error-Custom "Linking failed!"
        if (Test-Path "$BuildDir\error_link.txt") {
            Write-Host (Get-Content "$BuildDir\error_link.txt" -Raw) -ForegroundColor Red
        }
        exit 1
    }
} catch {
    Write-Error-Custom "Exception while linking: $_"
    exit 1
}

# Verify output
if (Test-Path $OutputBinary) {
    $FileSize = (Get-Item $OutputBinary).Length
    $BinaryName = Split-Path $OutputBinary -Leaf
    
    Write-Host ""
    Write-Success "Build completed successfully!"
    Write-Host ""
    Write-Host "Output binary:" -ForegroundColor Cyan
    Write-Host "  Path: $OutputBinary" -ForegroundColor White
    Write-Host "  Size: $($FileSize / 1KB) KB" -ForegroundColor White
    Write-Host "  Architecture: $Architecture" -ForegroundColor White
    Write-Host "  Target: Android API $ApiLevel+" -ForegroundColor White
    Write-Host ""
    Write-Info "Next steps:"
    Write-Host "  1. Push to Android device:" -ForegroundColor Yellow
    Write-Host "     adb push $OutputBinary /data/local/tmp/" -ForegroundColor White
    Write-Host "  2. Make executable:" -ForegroundColor Yellow
    Write-Host "     adb shell chmod +x /data/local/tmp/$BinaryName" -ForegroundColor White
    Write-Host "  3. Push your target .so file:" -ForegroundColor Yellow
    Write-Host "     adb push target/your_lib.so /data/local/tmp/" -ForegroundColor White
    Write-Host "  4. Run on device:" -ForegroundColor Yellow
    Write-Host "     adb shell 'cd /data/local/tmp && LD_LIBRARY_PATH=. ./$BinaryName ./your_lib.so'" -ForegroundColor White
    Write-Host "  5. Pull logs:" -ForegroundColor Yellow
    Write-Host "     adb pull /data/local/tmp/logs/ logs/" -ForegroundColor White
    Write-Host ""
} else {
    Write-Error-Custom "Build failed - output file not created!"
    exit 1
}
