[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet('x86', 'x64')]
	[string] $Architecture,

	[Parameter(Mandatory = $true)]
	[string] $PackageDirectory,

	[Parameter(Mandatory = $true)]
	[string] $ArchivePath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$package = Get-Item -LiteralPath $PackageDirectory -ErrorAction Stop
if (-not $package.PSIsContainer) {
	throw "Package path is not a directory: $PackageDirectory"
}

$files = @(Get-ChildItem -LiteralPath $package.FullName -Recurse -File)
if ($files.Count -eq 0) {
	throw "Package is empty: $($package.FullName)"
}

$requiredFiles = @('NexaMap Editor.exe', 'LICENSE.rtf', 'README.md')
foreach ($requiredFile in $requiredFiles) {
	if (-not (Test-Path -LiteralPath (Join-Path $package.FullName $requiredFile) -PathType Leaf)) {
		throw "Required package file is missing: $requiredFile"
	}
}

$requiredDirectories = @('brushes', 'data', 'extensions', 'icons')
foreach ($requiredDirectory in $requiredDirectories) {
	$directory = Join-Path $package.FullName $requiredDirectory
	if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
		throw "Required package directory is missing: $requiredDirectory"
	}
	if (-not (Get-ChildItem -LiteralPath $directory -Recurse -File | Select-Object -First 1)) {
		throw "Required package directory is empty: $requiredDirectory"
	}
}

$forbiddenDirectoryPattern = '(^|[\\/])(CMakeFiles|buildtrees|packages|vcpkg_installed)([\\/]|$)'
$forbiddenExtensions = @('.exp', '.ilk', '.lib', '.log', '.obj', '.pch', '.pdb')
foreach ($file in $files) {
	$relativePath = [IO.Path]::GetRelativePath($package.FullName, $file.FullName)
	if ($relativePath -match $forbiddenDirectoryPattern -or $forbiddenExtensions -contains $file.Extension.ToLowerInvariant()) {
		throw "Intermediate build file found in package: $relativePath"
	}
}

$executablePath = Join-Path $package.FullName 'NexaMap Editor.exe'
$bytes = [IO.File]::ReadAllBytes($executablePath)
if ($bytes.Length -lt 64) {
	throw 'Executable is too small to contain a valid PE header.'
}

$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 0 -or $peOffset + 26 -gt $bytes.Length) {
	throw 'Executable contains an invalid PE header offset.'
}

$signature = [Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4)
if ($signature -ne "PE`0`0") {
	throw 'Executable does not contain a valid PE signature.'
}

$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
$optionalHeaderMagic = [BitConverter]::ToUInt16($bytes, $peOffset + 24)
$expected = if ($Architecture -eq 'x86') {
	@{ Machine = 0x014c; Magic = 0x010b; Description = 'PE32 / Intel 386' }
} else {
	@{ Machine = 0x8664; Magic = 0x020b; Description = 'PE32+ / AMD64' }
}

if ($machine -ne $expected.Machine -or $optionalHeaderMagic -ne $expected.Magic) {
	throw ('Architecture mismatch for {0}: machine=0x{1:X4}, optional-header=0x{2:X4}; expected machine=0x{3:X4}, optional-header=0x{4:X4}.' -f `
		$Architecture, $machine, $optionalHeaderMagic, $expected.Machine, $expected.Magic)
}

$resolvedArchivePath = [IO.Path]::GetFullPath($ArchivePath)
if (Test-Path -LiteralPath $resolvedArchivePath) {
	Remove-Item -LiteralPath $resolvedArchivePath -Force
}

Compress-Archive -Path (Join-Path $package.FullName '*') -DestinationPath $resolvedArchivePath -CompressionLevel Optimal
if (-not (Test-Path -LiteralPath $resolvedArchivePath -PathType Leaf)) {
	throw "Archive was not created: $resolvedArchivePath"
}

$archive = [IO.Compression.ZipFile]::OpenRead($resolvedArchivePath)
try {
	$entries = @($archive.Entries | Where-Object { -not [string]::IsNullOrEmpty($_.Name) })
	if ($entries.Count -eq 0) {
		throw 'Archive is empty.'
	}

	$entryNames = @($entries | ForEach-Object { $_.FullName.Replace('\', '/') })
	if ($entryNames -notcontains 'NexaMap Editor.exe') {
		throw 'Archive does not contain NexaMap Editor.exe at its root.'
	}
	foreach ($requiredDirectory in $requiredDirectories) {
		if (-not ($entryNames | Where-Object { $_.StartsWith("$requiredDirectory/") } | Select-Object -First 1)) {
			throw "Archive does not contain runtime directory: $requiredDirectory"
		}
	}

	foreach ($entryName in $entryNames) {
		$extension = [IO.Path]::GetExtension($entryName).ToLowerInvariant()
		if ($entryName -match $forbiddenDirectoryPattern -or $forbiddenExtensions -contains $extension) {
			throw "Intermediate build file found in archive: $entryName"
		}
	}
} finally {
	$archive.Dispose()
}

$archiveInfo = Get-Item -LiteralPath $resolvedArchivePath
Write-Host ('Validated {0}: {1}, {2} files, {3:N2} MiB uncompressed, {4:N2} MiB ZIP.' -f `
	$Architecture, $expected.Description, $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), ($archiveInfo.Length / 1MB))
