# VBS enclave A0 experiment

This directory is a source-only, default-absent A0 compile probe. The repository's
ordinary CMake graph does not discover it, and it is not installed or exported. Its
two projects select C++20 locally; ordinary MEXCE remains C++11.

The probe defines one primary-enclave entry. Microsoft VBS Enclave Code Generator
owns the host/enclave boundary and generates its marshaling from `a0_entry.edl`.
The request and response are separate fixed-width, 8-byte structures. The enclave
copies their two fields and has no MEXCE expression behavior or product contract.
There are no application-defined, product, or untrusted EDL callbacks. The host calls
Microsoft's `register_callbacks` and generated `RegisterVtl0Callbacks` only to install
the framework allocation plumbing required before generated copied-parameter methods.

This A0 does not contain keys, cryptography, persistence, attestation, MAA, issuer
logic, product APIs, packaging, installation, export rules, signing requests,
certificates, fallback paths, customer assets, or runtime-security claims. Its
zero-filled host owner ID and source-level family/image IDs are compile-probe values,
not deployable identities. Do not run the host or distribute either output.

## Candidate package evidence

The packages were downloaded without credentials from NuGet's official flat-container
endpoint into `%TEMP%\mexce-a0-vbs-packages-20260813`; no package binary is in Git.
On 2026-08-13, `dotnet nuget verify --all` accepted both the Microsoft author signature
and the NuGet.org repository signature for every package below.

| Package | Version | NUPKG SHA-256 | Bytes | License |
| --- | --- | --- | ---: | --- |
| `Microsoft.Windows.VbsEnclave.CodeGenerator` | `0.2.260211.1` | `37A1091F05C70EC6F2DB25A495976FF900C8E64ED25871F6A10C0C8EE81F0D3F` | `17,225,578` | MIT; bundled notices also identify Open Enclave/Intel EDL parser and FlatBuffers |
| `Microsoft.Windows.VbsEnclave.SDK` | `0.2.260223.1` | `D76E7F141C842BA25761979437952D4B3E343534912DCEBA848482A1F8FC4174` | `7,224,411` | MIT |
| `Microsoft.Windows.ImplementationLibrary` | `1.0.240803.1` | `FBC8F63269C99BC551E41E48D258B9F011BBF4A5C3FA3F706307D5EBCF70B087` | `353,354` | MIT |
| `Microsoft.Windows.SDK.CPP` | `10.0.26100.7463` | `54C5E6EBFFF5E1A8A84CDBF8AB7A4E72419620A5B2D430A3AA88734C4C557CAB` | `160,353,403` | Microsoft Windows SDK license; acceptance required |
| `Microsoft.Windows.SDK.CPP.x64` | `10.0.26100.7463` | `7A7FF4028FAB1153B42B23BCAB20481BA8511CDBE4157A54A9DF5B6F46C3D179` | `53,126,785` | Microsoft Windows SDK license; acceptance required |

The accepted author certificate SHA-256 is
`566A31882BE208BE4422F7CFD66ED09F5D4524A5994F50CCC8B05EC0528C1353`;
the accepted repository certificate SHA-256 is
`1F4B311D9ACC115C8DC8018B5A49E00FCE6DA8E2855F9F014CA6F34570BC482D`.
The local `nuget.config` clears inherited feeds, maps these package IDs only to
nuget.org, clears fallback folders, requires signed packages, and pins that verified
repository signer. Both projects use exact bracketed versions and project-local
`packages.lock.json` files in locked mode. Those files bind the graph to the following
NuGet content hashes reported by signature verification:

- WIL: `RNqwQsASe40hh3AtxTbHE25pShdPpwnad4ehf3rjOB5El7++2icP4WnWyFy5LXG58VzLIsyaaIcBpvd8Kj6Xlg==`
- Windows SDK C++: `Y+hvp5x/AxiKe5h9HnGScDsQgh8bufxuxEJo5eMj92VYxn7O5ZmWmjwZjySLecbcpx9BPO8wBamtzwIIzSIa6w==`
- Windows SDK C++ x64: `R8ADM2dC6RaokwlcFHdLPyrApIJ3zN9fM7uOjelnzBDamq6LlcKaPzJrl0URRhZNMfeybrCWBrJXSokQU8EQxg==`
- Code Generator: `uWWKyoBVi1qSigX/oM0/ZXK7AiNII61kYjQyT9hpn8swxCRu2YYtwk06IzdfX9WaU+boNdwt+yBDwnN4BBVNqQ==`
- VBS Enclave SDK: `jJ7HEaO3G50vt3gfHktUrK5J8pBxAwD5O0DZS5Jmgkf0ZMj+rOr2FJwrg9TVp6MipukbByEr3Ug7Zdes29e+0A==`

The inspected SDK license file has SHA-256
`DD07EB178E00C6BBA4148457FC00FF77CD4887EB521D504186FE59C9EC8BBE62`.
An executor or organization must accept those Windows SDK terms before restore/use.
A0 does not grant redistribution rights and does not vendor SDK material.

The Code Generator and VBS Enclave SDK package metadata each asks for WIL
`1.0.240803.1` or later and Windows SDK C++ `10.0.26100.7175` or later. The SDK's
embedded README and current official source instead require Windows SDK
`10.0.26100.7463` or later, so A0 takes the smaller conservative path and pins
`10.0.26100.7463`. The base SDK package says an architecture package is required but
does not declare one; A0 therefore pins the matching x64 package directly.

The Code Generator embeds ABI version `1`. Its `edlcodegen.exe` has SHA-256
`A75BCCB6299B4316EB9A8A6DBF4AF16CDDDFBA41DA689D44E93FACEE1D173B25`
and a valid Microsoft Authenticode signature. Its bundled `flatc.exe` has SHA-256
`5C47EE84F6D86A426C63F97D392D450C6309505147BE54DC37D4C2ACECF7D053`
and a valid Microsoft third-party-component Authenticode signature.

The SDK packages do not contain `veiid.exe`. The installed x64 candidate was inspected
without execution at
`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\veiid.exe`:
version `10.0.26100.8249`, 43,432 bytes, SHA-256
`70D1C3F32661A57DB92BCE8A8035B270BB5E4F3BAD85B754B0E9D7E02C1E5F82`, with a valid
Microsoft Authenticode signature. It is newer than the pinned `7463` SDK libraries;
the exact mixed-version combination has not been exercised and remains an explicit
pre-build approval item.

Package pages:

- <https://www.nuget.org/packages/Microsoft.Windows.VbsEnclave.CodeGenerator/0.2.260211.1>
- <https://www.nuget.org/packages/Microsoft.Windows.VbsEnclave.SDK/0.2.260223.1>
- <https://www.nuget.org/packages/Microsoft.Windows.ImplementationLibrary/1.0.240803.1>
- <https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP/10.0.26100.7463>
- <https://www.nuget.org/packages/Microsoft.Windows.SDK.CPP.x64/10.0.26100.7463>
- Windows SDK license: <https://aka.ms/WinSDKLicenseURL>

## Source and integration evidence

Microsoft's official documentation requires a primary image for host-callable
exports and documents `/ENCLAVE`, non-incremental linking, integrity checking, mixed
guard metadata, no default libraries, the enclave CRT/library set, and VEIID
processing after link. It strongly recommends boundary copies through
`EnclaveCopyIntoEnclave` and `EnclaveCopyOutOfEnclave`; the generated layer performs
those copies. A0 enables strict-memory policy and explicitly excludes both the
enclave debug policy and host debug flag.

- Overview: <https://learn.microsoft.com/windows/win32/trusted-execution/vbs-enclaves>
- Development guide: <https://learn.microsoft.com/windows/win32/trusted-execution/vbs-enclaves-dev-guide>
- Official tooling source: <https://github.com/microsoft/VbsEnclaveTooling>
- WIL source identified by its package: <https://github.com/microsoft/wil>
- Windows SDK project identified by its packages: <https://aka.ms/WinSDKProjectURL>

The official source inspected outside this repository was `main` commit
`de1c245be47c9fe518aee1b9c0f2cc73cedb3e4b` (2026-08-03). The package manifests
identify that project URL but do not contain repository commit metadata, so that
source commit cannot be proven to be the exact source of these package builds. The
source and samples are design evidence only; the signed package contents and hashes
above are the candidate artifact evidence.

Both candidate VBS packages expose MSBuild-native `.props`/`.targets` integration:
the targets invoke `edlcodegen.exe`, add generated source wildcards, inject enclave
libraries, and copy the SDK runtime. The isolated native MSBuild projects are the
smallest reversible way to exercise that official integration. Recreating those
steps in CMake/FASTBuild would be a second, unverified integration and is out of A0.
The inspected Windows SDK NuGet packages do not contain `veiid.exe`; the enclave
project therefore resolves that tool from the installed Windows Kit and fails closed
when it is absent. It does not download or substitute a tool.

The recovery plan was not edited in this A0 slice. It was separately clarified to
distinguish mandatory Microsoft-generated framework allocation plumbing from
application-defined, product, or untrusted EDL callbacks.

## Unavailable gates and queued boundary

No package restore, generation, configure, compilation, link, VEIID processing,
signing, or runtime test was performed. The current host is Windows build `22631`,
has a pending reboot, and does not provide the required VBS/HVCI and Secure Boot
conditions. Current Microsoft tooling source requires Windows 11 build `26100.3916`
or later; the documentation also requires Windows SDK/compilers and VBS enclave
support.

Before the first build, independently verify all of the following:

1. The reboot is complete and no reboot remains pending.
2. The OS is at least build `26100.3916` and VBS/HVCI plus Secure Boot are available.
3. Visual Studio 2026 is complete and launchable with MSVC v145.
4. The signed package hashes and license acceptance are still approved.
5. The installed x64 `veiid.exe` identity and its compatibility with the pinned SDK
   libraries are approved; override `A0VeiidPath` explicitly only for another
   reviewed installed tool.
6. A new empty `RestorePackagesPath` is allocated for this run; no global/fallback
   package folder or HTTP cache is allowed.

Then run exactly one queued Release/x64 solution build from a Visual Studio 2026 x64
developer environment:

```powershell
$a0_restore_root = Join-Path $env:TEMP ("mexce-vbs-a0-restore-" + [guid]::NewGuid().ToString("N"))
$a0_restore_packages = Join-Path $a0_restore_root "packages"
New-Item -ItemType Directory -Path $a0_restore_packages | Out-Null

queued-build --slots 2 -- msbuild `
    experimental\vbs_enclave_a0\vbs_enclave_a0.sln `
    /restore /t:Build `
    /p:RestoreConfigFile=experimental\vbs_enclave_a0\nuget.config `
    /p:RestorePackagesPath="$a0_restore_packages" `
    /p:RestoreLockedMode=true `
    /p:RestoreNoHttpCache=true `
    /p:RestoreFallbackFolders= `
    /p:RestoreAdditionalProjectFallbackFolders= `
    /p:RestoreAdditionalProjectFallbackFoldersExcludes= `
    /p:DisableImplicitNuGetFallbackFolder=true `
    /p:Configuration=Release /p:Platform=x64 /m:2
```

The native NuGet targets make this graph MSBuild-specific, which is the documented
FASTBuild incompatibility for this bounded probe. The enclave project invokes VEIID
after link and contains no signing step. After that eligible build, inspect the x64
Release image headers, imports, exports, debug directories, load configuration,
enclave configuration, and VEIID-updated import identities under a one-slot queued
inspection command. Do not execute or sign the host or enclave during A0.

```powershell
queued-build --slots 1 -- dumpbin /headers /imports /exports /loadconfig experimental\vbs_enclave_a0\out\x64\Release\a0_enclave.dll
```
