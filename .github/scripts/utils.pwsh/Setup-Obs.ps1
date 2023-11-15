function Setup-Obs {
    if ( ! ( Test-Path function:Log-Output ) ) {
        . $PSScriptRoot/Logger.ps1
    }

    if ( ! ( Test-Path function:Check-Git ) ) {
        . $PSScriptRoot/Check-Git.ps1
    }

    Check-Git

    if ( ! ( Test-Path function:Ensure-Location ) ) {
        . $PSScriptRoot/Ensure-Location.ps1
    }

    if ( ! ( Test-Path function:Invoke-GitCheckout ) ) {
        . $PSScriptRoot/Invoke-GitCheckout.ps1
    }

    if ( ! ( Test-Path function:Invoke-External ) ) {
        . $PSScriptRoot/Invoke-External.ps1
    }

    Log-Information 'Setting up OBS Studio...'

    $ObsVersion = $BuildSpec.dependencies.'obs-studio'.version
    $ObsRepository = $BuildSpec.dependencies.'obs-studio'.repository
    $ObsBranch = $BuildSpec.dependencies.'obs-studio'.branch
    $ObsHash = $BuildSpec.dependencies.'obs-studio'.hash

    if ( $ObsVersion -eq '' ) {
        throw 'No obs-studio version found in buildspec.json.'
    }

    Push-Location -Stack BuildTemp
    Ensure-Location -Path "$(Resolve-Path -Path "${ProjectRoot}/../")/obs-studio"

    if ( ! ( ( $script:SkipAll ) -or ( $script:SkipUnpack ) ) ) {
        Invoke-GitCheckout -Uri $ObsRepository -Commit $ObsHash -Path . -Branch $ObsBranch
    }

    if ( ! ( ( $script:SkipAll ) -or ( $script:SkipBuild ) ) ) {
        Log-Information 'Configuring OBS Studio...'

        $NumProcessors = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors

        if ( $NumProcessors -gt 1 ) {
            $env:UseMultiToolTask = $true
            $env:EnforceProcessCountAcrossBuilds = $true
        }

        $DepsPath = "plugin-deps-${script:DepsVersion}-qt${script:QtVersion}-${script:Target}"

        $CmakeArgs = @(
            '-G', $CmakeGenerator
            "-DCMAKE_SYSTEM_VERSION=${script:PlatformSDK}"
            "-DCMAKE_GENERATOR_PLATFORM=$(if (${script:Target} -eq "x86") { "Win32" } else { "x64" })"
            "-DCMAKE_BUILD_TYPE=${script:Configuration}"
            "-DQT_VERSION=${script:QtVersion}"
            '-DENABLE_PLUGINS=OFF'
            '-DENABLE_BROWSER=OFF'
            '-DBUILD_BROWSER=OFF'
            '-DENABLE_UI=OFF'
            '-DENABLE_SCRIPTING=OFF'
            "-DCMAKE_INSTALL_PREFIX:PATH=$(Resolve-Path -Path "${ProjectRoot}/../obs-build-dependencies/${DepsPath}")"
            "-DCMAKE_PREFIX_PATH:PATH=$(Resolve-Path -Path "${ProjectRoot}/../obs-build-dependencies/${DepsPath}")"
            "-DDepsPath:PATH=$(Resolve-Path -Path "${ProjectRoot}/../obs-build-dependencies/${DepsPath}")"
            "-DQTDIR:PATH=$(Resolve-Path -Path "${ProjectRoot}/../obs-build-dependencies/${DepsPath}")"
            "-D_CURL_LIBRARIES=libcurl_imp"
        )

        Log-Debug "Attempting to configure OBS with CMake arguments: $($CmakeArgs | Out-String)"
        Log-Information "Configuring OBS..."
        Invoke-External cmake -S . -B plugin_build_${script:Target} @CmakeArgs

        Log-Information 'Building libobs and obs-frontend-api...'
        $CmakeArgs = @(
            '--config', "$( if ( $script:Configuration -eq '' ) { 'RelWithDebInfo' } else { $script:Configuration })"
        )

        if ( $VerbosePreference -eq 'Continue' ) {
            $CmakeArgs+=('--verbose')
        }

        Invoke-External cmake --build plugin_build_${script:Target} @CmakeArgs -t obs-frontend-api
        Invoke-External cmake --install plugin_build_${script:Target} @CmakeArgs --component obs_libraries
        # Temporary compatibility with legacy OBS for now
        Invoke-External cmake --install plugin_build_${script:Target}\UI\obs-frontend-api @CmakeArgs
        Invoke-External cmake --install plugin_build_${script:Target}\libobs @CmakeArgs
        Invoke-External cmake --install plugin_build_${script:Target}\deps\w32-pthreads @CmakeArgs
        # Don't bother forking legacy OBS to fix installation issues
        if ( ! ( Test-Path -Path "${ProjectRoot}/../obs-build-dependencies/${DepsPath}/bin/64bit/obs-frontend-api.lib") ) {
          Copy-Item "plugin_build_${script:Target}/UI/obs-frontend-api/$( if ( $script:Configuration -eq '' ) { 'RelWithDebInfo' } else { $script:Configuration })/obs-frontend-api.lib" "${ProjectRoot}/../obs-build-dependencies/${DepsPath}/bin/64bit"
        }
        $filemod = "${ProjectRoot}/../obs-build-dependencies/${DepsPath}/include/util/threading.h"
        (Get-Content $filemod).replace('../../deps/w32-pthreads/pthread.h', 'pthread.h') | Set-Content $filemod

        # Or still current issues
        Copy-Item "deps/w32-pthreads/pthread.h" "${ProjectRoot}/../obs-build-dependencies/${DepsPath}/include"
        Copy-Item "deps/w32-pthreads/sched.h" "${ProjectRoot}/../obs-build-dependencies/${DepsPath}/include"

    }
    Pop-Location -Stack BuildTemp
}
