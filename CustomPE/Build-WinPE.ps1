# =============================================
#  CustomPE - Custom Windows PE for OEM Deployment
#  Author: AlexMartin
#  Last updated: 2026-08-07
# =============================================
<#
.SYNOPSIS
    Build-WinPE.ps1 - Build custom Windows PE ISO with external-script interface
.DESCRIPTION
    Single build implementation (invoked by Build-WinPE.cmd or directly).
    All tunable data lives in Build-WinPE.conf next to this script.
#>

param(
    [string]$WorkDir = "",
    [string]$OutputPath = "",
    [string]$ADKPath = "",
    [string]$DriverRoot = "",
    [switch]$SkipDrivers,
    [switch]$SkipComponents
)

# Load global configuration (Build-WinPE.conf). Repeated keys are accumulated
# (used by REG_ADD); all other keys are single-valued.
$confFile = Join-Path $PSScriptRoot "Build-WinPE.conf"
$conf = @{}
if (Test-Path $confFile) {
    Get-Content -Path $confFile -Encoding UTF8 | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#") -and $line.Contains("=")) {
            $kv = $line.Split("=", 2)
            $key = $kv[0].Trim()
            $val = $kv[1].Trim()
            if ($conf.ContainsKey($key)) { $conf[$key] = $conf[$key] + "`n" + $val }
            else { $conf[$key] = $val }
        }
    }
} else {
    Write-Warning "Build-WinPE.conf not found next to this script; using built-in defaults."
}

# Resolve project identity
$CustomPE_Name    = if ($conf.ContainsKey("CustomPE_NAME"))    { $conf["CustomPE_NAME"] }    else { "CustomPE" }
$CustomPE_Author  = if ($conf.ContainsKey("CustomPE_AUTHOR"))  { $conf["CustomPE_AUTHOR"] }  else { "AlexMartin" }
$CustomPE_Version = "v" + $(if ($conf.ContainsKey("CustomPE_VERSION")) { $conf["CustomPE_VERSION"] } else { "1.0" })
$buildDate        = Get-Date -Format 'yyyy-MM-dd'
$WinPE_Arch       = "amd64"

# Resolve build workspace and driver repository
$suiteRoot = Split-Path $PSScriptRoot -Parent
if (-not $WorkDir) {
    if ($conf.ContainsKey("CustomPE_WORKDIR") -and $conf["CustomPE_WORKDIR"]) { $WorkDir = $conf["CustomPE_WORKDIR"] }
    else { $WorkDir = Join-Path $suiteRoot "Build-WorkDir" }
}
if (-not $OutputPath) { $OutputPath = "$WorkDir\ISO\$CustomPE_Name.iso" }
if (-not $DriverRoot) {
    if ($conf.ContainsKey("CustomPE_DRVREPO") -and $conf["CustomPE_DRVREPO"]) { $DriverRoot = $conf["CustomPE_DRVREPO"] }
    else {
        $probeDrv = Join-Path $suiteRoot "DriversRepo"
        if (Test-Path -LiteralPath $probeDrv) { $DriverRoot = $probeDrv } else { $DriverRoot = "E:\DriversRepo" }
    }
}

Write-Host "============================================"

# Resolve the media-content source folder once: <CustomPE_NAME> first, legacy
# "CustomPE" fallback. It drives both the host build-log location (below) and
# the ISO copy in Step 9, so a renamed project keeps everything under one name.
$customPESrc = Join-Path $PSScriptRoot $CustomPE_Name
if (-not (Test-Path -LiteralPath $customPESrc)) {
    $legacySrc = Join-Path $PSScriptRoot "CustomPE"
    if (Test-Path -LiteralPath $legacySrc) { $customPESrc = $legacySrc } else { $customPESrc = $null }
}

# Logging setup: real-time transcript into the content folder's Logs
# (auto-cleared each run). If no content folder exists yet, fall back to a
# plain project-root Logs folder so no empty <CustomPE_NAME> folder is created.
$logsDir = if ($customPESrc) { Join-Path $customPESrc "Logs" } else { Join-Path $PSScriptRoot "Logs" }
if (-not (Test-Path $logsDir)) { New-Item -Path $logsDir -ItemType Directory -Force | Out-Null }
Get-ChildItem -Path $logsDir -Filter *.log -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$logFile = Join-Path $logsDir "Build.log"
Start-Transcript -Path $logFile -Force | Out-Null
Write-Host "  Log: $logFile"
Write-Host "  $CustomPE_Name Build Script $CustomPE_Version"
Write-Host "  Target: Windows 11 24H2 $WinPE_Arch"
Write-Host "============================================"

# Check admin
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$princ = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $princ.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "Not running as Administrator. Some operations may fail."
}

# Auto-detect ADK path (probes common install roots, then overrides)
if (-not $ADKPath) {
    $peRoots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\Assessment and Deployment Kit\Windows Preinstallation Environment",
        "${env:ProgramFiles}\Windows Kits\10\Assessment and Deployment Kit\Windows Preinstallation Environment",
        "D:\Windows Kits\10\Assessment and Deployment Kit\Windows Preinstallation Environment",
        "E:\Windows Kits\10\Assessment and Deployment Kit\Windows Preinstallation Environment"
    )
    foreach ($pe in $peRoots) {
        if (Test-Path "$pe\$WinPE_Arch") { $ADKPath = "$pe\$WinPE_Arch"; break }
    }
    if (-not $ADKPath) {
        Write-Error "ADK not found. Install Windows ADK + WinPE Add-on, or pass -ADKPath."
        Stop-Transcript -ErrorAction SilentlyContinue
        exit 1
    }
    Write-Host "[OK] ADK: $ADKPath"
}

# Set the ADK environment used by copype.cmd (WinPERoot / OSCDImgRoot / DISMRoot)
$adkPeRoot  = Split-Path $ADKPath -Parent
$kitsAdk    = Split-Path $adkPeRoot -Parent
$dtArchRoot = Join-Path $kitsAdk "Deployment Tools\$WinPE_Arch"
$env:WinPERoot   = $adkPeRoot
$env:OSCDImgRoot = Join-Path $dtArchRoot "Oscdimg"
$env:DISMRoot    = Join-Path $dtArchRoot "DISM"
foreach ($tool in @("DISM","Imaging","BCDBoot","Oscdimg","Wdsmcast")) {
    $toolDir = Join-Path $dtArchRoot $tool
    if ((Test-Path $toolDir) -and ($env:Path -notlike "*$toolDir*")) { $env:Path = "$toolDir;$env:Path" }
}
Write-Host "    ADK env: WinPERoot=$env:WinPERoot"

# Step 1: Clean
Write-Host "`n[Step 1/9] Cleaning environment..."
dism /Cleanup-Mountpoints 2>&1 | Out-Null
dism /Cleanup-Wim 2>&1 | Out-Null
if (Test-Path $WorkDir) {
    Write-Host "  Removing old work directory: $WorkDir"
    Remove-Item -Path $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "  [OK]"

# Step 2: Create base WinPE
Write-Host "`n[Step 2/9] Creating base WinPE via copype.cmd..."
$copype = "$(Split-Path $ADKPath -Parent)\copype.cmd"
if (-not (Test-Path $copype)) {
    Write-Error "copype.cmd not found at: $copype"
    Stop-Transcript -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "  Running: copype.cmd $WinPE_Arch $WorkDir"
& cmd.exe /c """$copype"" $WinPE_Arch ""$WorkDir""" 2>&1 | ForEach-Object { Write-Host "    $_" }
if (-not (Test-Path "$WorkDir\media\Sources\boot.wim")) {
    Write-Error "copype.cmd failed - boot.wim not found"
    Stop-Transcript -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "  [OK]"

# Step 2b: Clean ISO structure (OEM style) - locale list comes from Build-WinPE.conf
Write-Host "`n[Step 2b/9] Cleaning ISO structure to OEM style..."
$Mediadir = "$WorkDir\media"
$localeDirs = @()
if ($conf.ContainsKey("LOCALE_DIRS")) {
    $localeDirs = @($conf["LOCALE_DIRS"] -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
if ($localeDirs.Count -eq 0) {
    Write-Warning "  LOCALE_DIRS not defined in Build-WinPE.conf; skipping locale move."
} else {
    foreach ($d in $localeDirs) {
        $src = Join-Path $Mediadir $d
        $dst = Join-Path $Mediadir "Boot\$d"
        if (Test-Path $src) {
            if (-not (Test-Path $dst)) { New-Item -Path $dst -ItemType Directory -Force | Out-Null }
            Move-Item -Path "$src\*" -Destination $dst -Force -ErrorAction SilentlyContinue
            Remove-Item -Path $src -Force -ErrorAction SilentlyContinue
            Write-Host "    Moved: $d -> Boot\$d"
        }
    }
}
Write-Host "  [OK]"

# Step 3: Mount boot.wim
Write-Host "`n[Step 3/9] Mounting boot.wim..."
$MountDir = "$WorkDir\mount"
if (-not (Test-Path $MountDir)) { New-Item -Path $MountDir -ItemType Directory -Force | Out-Null }
dism /Mount-Image /ImageFile:"$WorkDir\media\Sources\boot.wim" /Index:1 /MountDir:"$MountDir" 2>&1 | ForEach-Object { Write-Host "    $_" }
if (-not (Test-Path "$MountDir\Windows")) {
    Write-Error "Mount failed"
    Stop-Transcript -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "  [OK]"

# Step 4: Inject WinPE optional components (data-driven from Build-WinPE.conf)
if (-not $SkipComponents) {
    Write-Host "`n[Step 4/9] Injecting WinPE optional components..."
    $OCS = ""
    if ($conf.ContainsKey("CustomPE_OCSPATH") -and $conf["CustomPE_OCSPATH"]) {
        $OCS = $conf["CustomPE_OCSPATH"]
        if (-not (Test-Path $OCS)) {
            Write-Warning "  CustomPE_OCSPATH not found: $OCS (falling back to ADK auto-detect)"
            $OCS = ""
        }
    }
    if (-not $OCS) {
        $autoOcs = Join-Path $ADKPath "WinPE_OCs"
        if (Test-Path $autoOcs) { $OCS = $autoOcs }
    }
    if (-not (Test-Path $OCS)) {
        Write-Warning "  WinPE_OCs not found. Skipping component injection."
    } else {
        $groups = @(
            @{ Id = "5.1"; Title = "Core Management & Scripting";          Key = "COMPONENTS_51" }
            @{ Id = "5.2"; Title = ".NET Framework + PowerShell";          Key = "COMPONENTS_52" }
            @{ Id = "5.3"; Title = "Storage, Security & Deployment Tools"; Key = "COMPONENTS_53" }
            @{ Id = "5.4"; Title = "Font Support";                         Key = "COMPONENTS_54" }
        )
        $compIssue = 0
        foreach ($g in $groups) {
            Write-Host "  [$($g.Id)] $($g.Title)"
            $list = @()
            if ($conf.ContainsKey($g.Key)) {
                $list = @($conf[$g.Key] -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
            } else {
                Write-Warning "    No $($g.Key) defined in Build-WinPE.conf"
            }
            foreach ($c in $list) {
                $cp = Join-Path $OCS $c
                if (-not (Test-Path $cp)) { Write-Warning "    MISSING: $c"; $compIssue++; continue }
                Write-Host "    $c"
                dism /Image:"$MountDir" /Add-Package /PackagePath:"$cp" 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { Write-Warning "    FAILED: $c (dism exit code $LASTEXITCODE)"; $compIssue++ }
                $base = $c -replace '\.cab$',''
                $lc = Join-Path $OCS "en-us\${base}_en-us.cab"
                if (Test-Path $lc) {
                    Write-Host "    ${base}_en-us.cab (language)"
                    dism /Image:"$MountDir" /Add-Package /PackagePath:"$lc" 2>&1 | Out-Null
                    if ($LASTEXITCODE -ne 0) { Write-Warning "    FAILED lang pack: ${base}_en-us.cab (exit $LASTEXITCODE)"; $compIssue++ }
                }
            }
        }
        if ($compIssue -gt 0) {
            Write-Error "  $compIssue component package issue(s) (missing or dism failure). Aborting build."
            dism /Unmount-Image /MountDir:"$MountDir" /Discard 2>&1 | Out-Null
            Stop-Transcript -ErrorAction SilentlyContinue
            exit 1
        } else {
            Write-Host "  [OK]"
        }
    }
}

# Step 5: Inject drivers (dynamic scan of $DriverRoot, any nesting, no fixed folder names)
if (-not $SkipDrivers) {
    Write-Host "`n[Step 5/9] Injecting hardware drivers..."
    if (-not (Test-Path $DriverRoot)) {
        Write-Host "    [WARN] Driver repository not found: $DriverRoot"
    } else {
        Write-Host "    Scanning: $DriverRoot"
        # Single pass: map driver folder -> .inf count (handles arbitrary nesting)
        $driverMap = @{}
        Get-ChildItem -Path $DriverRoot -Recurse -Filter '*.inf' -File -ErrorAction SilentlyContinue |
            ForEach-Object { $dir = $_.Directory.FullName; $driverMap[$dir] = [int]$driverMap[$dir] + 1 }
        $driverDirs = @($driverMap.Keys | Sort-Object)

        if ($driverDirs.Count -eq 0) {
            Write-Host "    [WARN] No .inf driver packages found under $DriverRoot"
        } else {
            Write-Host "    Found $($driverDirs.Count) driver folders:"
            foreach ($d in $driverDirs) {
                Write-Host "      $d ($($driverMap[$d]) .inf)"
            }
            $drvIssue = 0
            foreach ($d in $driverDirs) {
                Write-Host "    Injecting: $d"
                dism /Image:"$MountDir" /Add-Driver /Driver:"$d" 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { Write-Warning "    Driver injection failed: $d (exit $LASTEXITCODE)"; $drvIssue++ }
            }
            if ($drvIssue -gt 0) {
                Write-Warning "  $drvIssue driver folder(s) failed to inject."
            } else {
                Write-Host "  [OK]"
            }
        }
    }
}

# Step 6: Configure registry (data-driven from REG_ADD in Build-WinPE.conf)
Write-Host "`n[Step 6/9] Configuring offline registry..."
$hive = "$MountDir\Windows\System32\config\SOFTWARE"
if (Test-Path $hive) {
    reg load HKLM\CustomPE_SOFTWARE "$hive" 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        $regAdds = @()
        if ($conf.ContainsKey("REG_ADD")) { $regAdds = @($conf["REG_ADD"] -split "`n") }
        foreach ($r in $regAdds) {
            $parts = @($r -split '\|')
            if ($parts.Count -lt 5) { Write-Warning "    Invalid REG_ADD entry: $r"; continue }
            $relKey = $parts[0].Trim()
            $name   = $parts[1].Trim()
            $type   = $parts[2].Trim()
            $data   = $parts[3].Trim()
            $desc   = $parts[4].Trim()
            Write-Host "    $desc"
            reg add "HKLM\CustomPE_SOFTWARE\$relKey" /v "$name" /t $type /d "$data" /f 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { Write-Warning "    reg add failed: $relKey\$name" }
        }
        reg unload HKLM\CustomPE_SOFTWARE 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Warning "  Reg unload failed (may be fine in PE context)" }
        Write-Host "  [OK]"
    } else {
        Write-Warning "  Failed to load registry hive."
    }
}

# Step 7: Inject custom startnet.cmd (external interface), stamping the build date
Write-Host "`n[Step 7/9] Injecting custom startnet.cmd (external interface)..."
$startnetSrc = Join-Path $PSScriptRoot "Scripts\startnet.cmd"
if (Test-Path $startnetSrc) {
    $startnetText = [System.IO.File]::ReadAllText($startnetSrc)
    $startnetText = [regex]::Replace($startnetText, '(?m)(Built: )\d{4}-\d{2}-\d{2}', "`${1}$buildDate")
    $startnetDst = "$MountDir\Windows\System32\startnet.cmd"
    [System.IO.File]::WriteAllText($startnetDst, $startnetText, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "    Source: $startnetSrc"
    Write-Host "    Target: $startnetDst"
    Write-Host "    Build date stamped: $buildDate"
    Write-Host "  [OK]"
} else {
    Write-Warning "  startnet.cmd not found at $startnetSrc"
}

# Step 8: Optimize & commit
Write-Host "`n[Step 8/9] Optimizing and committing..."
Write-Host "    Running component cleanup..."
dism /Image:"$MountDir" /Cleanup-Image /StartComponentCleanup /ResetBase 2>&1 | ForEach-Object { Write-Host "    $_" }
if ($LASTEXITCODE -ne 0) { Write-Warning "  Component cleanup reported an error (exit $LASTEXITCODE); continuing to commit." }
Write-Host "    Committing and unmounting..."
dism /Unmount-Image /MountDir:"$MountDir" /Commit 2>&1 | ForEach-Object { Write-Host "    $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to commit boot.wim"
    Stop-Transcript -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "  [OK]"

# Step 9: Build ISO
Write-Host "`n[Step 9/9] Building ISO..."
$MediaDir = "$WorkDir\media"

# Copy external interface files
$extFiles = @(
    @{Src=Join-Path $PSScriptRoot "AutoRun.cmd"; Dst="$MediaDir\AutoRun.cmd"}
)
foreach ($f in $extFiles) {
    if (Test-Path $f.Src) {
        Copy-Item -Path $f.Src -Destination $f.Dst -Force
        Write-Host "    Added: $(Split-Path $f.Dst -Leaf)"
    }
}

# Copy tools directory (folder name follows CustomPE_NAME; content comes from the
# source folder resolved at startup - <CustomPE_NAME> first, legacy "CustomPE" fallback)
$customPEDst = "$MediaDir\$CustomPE_Name"
if ($customPESrc -and (Test-Path -LiteralPath $customPESrc)) {
    if (-not (Test-Path $customPEDst)) { New-Item -Path $customPEDst -ItemType Directory -Force | Out-Null }
    Copy-Item -Path "$customPESrc\*" -Destination $customPEDst -Recurse -Force
    Write-Host "    Added: $CustomPE_Name\ (tools & scripts)"
    # Ship an empty Logs folder (runtime logs are generated on first boot)
    $mediaLogs = Join-Path $customPEDst "Logs"
    if (-not (Test-Path $mediaLogs)) { New-Item -Path $mediaLogs -ItemType Directory -Force | Out-Null }
    Get-ChildItem -Path $mediaLogs -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
} else {
    Write-Warning "  Tools source folder not found ($CustomPE_Name or CustomPE); skipping tools & scripts copy."
}

# Copy global configuration to ISO root
if (Test-Path $confFile) {
    Copy-Item -Path $confFile -Destination "$MediaDir\Build-WinPE.conf" -Force
    Write-Host "    Added: Build-WinPE.conf"
}

# Create ISO output dir
$isoDir = Split-Path $OutputPath -Parent
if (-not (Test-Path $isoDir)) { New-Item -Path $isoDir -ItemType Directory -Force | Out-Null }

# Build ISO
$mwm = Join-Path (Split-Path $ADKPath -Parent) "MakeWinPEMedia.cmd"
Write-Host "    Running: $mwm /ISO $WorkDir $OutputPath"
& $mwm /ISO "$WorkDir" "$OutputPath" 2>&1 | ForEach-Object { Write-Host "    $_" }
if ($LASTEXITCODE -eq 0 -and (Test-Path $OutputPath)) {
    $size = (Get-Item $OutputPath).Length
    Write-Host "`n============================================"
    Write-Host "  BUILD SUCCESSFUL!"
    Write-Host "  ISO: $OutputPath"
    Write-Host "  Size: $([math]::Round($size/1MB, 2)) MB"
    Write-Host "============================================"
    Write-Host "`nExternal interface: AutoRun.cmd on ISO root"
    Write-Host "Edit AutoRun.cmd to customize deployment WITHOUT rebuilding boot.wim!"
} else {
    Write-Error "MakeWinPEMedia failed to create ISO."
    Stop-Transcript -ErrorAction SilentlyContinue
    exit 1
}

# Close transcript log
Stop-Transcript -ErrorAction SilentlyContinue
