#!/usr/bin/fish --no-config

function shuba_libjxl_usage
    printf '%s\n' \
        'Usage: tools/release/build-libjxl-android.fish [OPTION]' \
        '' \
        'With no option, build or reuse the validated fingerprinted dependency tree.' \
        '' \
        'Options:' \
        '  --clean               recreate only build/libjxl-android-arm64' \
        '  --check               verify the current cache without mutation' \
        '  --print-fingerprint   print the current configuration fingerprint only' \
        '  --help                show this help'
end

function shuba_libjxl_initialize_configuration
    set --global shuba_libjxl_release_version 0.12.0
    set --global shuba_libjxl_release_commit a7a9c787341cf703dede03c2009fa460cae5e5df
    set --global shuba_libjxl_output_inventory_version 2
    set --global shuba_libjxl_expected_archives \
        lib/libjxl.a lib/libjxl_cms.a lib/libjxl_dec.a lib/libjxl_threads.a \
        third_party/brotli/libbrotlicommon.a third_party/brotli/libbrotlidec.a \
        third_party/brotli/libbrotlienc.a third_party/highway/libhwy.a
    set --global shuba_libjxl_expected_headers \
        lib/include/jxl/cms.h lib/include/jxl/cms_interface.h \
        lib/include/jxl/codestream_header.h lib/include/jxl/color_encoding.h \
        lib/include/jxl/compressed_icc.h lib/include/jxl/decode_cxx.h \
        lib/include/jxl/decode.h lib/include/jxl/encode_cxx.h \
        lib/include/jxl/encode.h lib/include/jxl/gain_map.h \
        lib/include/jxl/jxl_cms_export.h lib/include/jxl/jxl_export.h \
        lib/include/jxl/jxl_threads_export.h lib/include/jxl/memory_manager.h \
        lib/include/jxl/parallel_runner.h lib/include/jxl/resizable_parallel_runner_cxx.h \
        lib/include/jxl/resizable_parallel_runner.h lib/include/jxl/stats.h \
        lib/include/jxl/thread_parallel_runner_cxx.h lib/include/jxl/thread_parallel_runner.h \
        lib/include/jxl/types.h lib/include/jxl/version.h
    set --global shuba_libjxl_build_targets \
        jxl jxl_cms jxl_dec jxl_threads brotlicommon brotlidec brotlienc hwy
    set --global shuba_libjxl_fixed_definitions \
        BUILD_SHARED_LIBS=OFF BUILD_TESTING=OFF CMAKE_POSITION_INDEPENDENT_CODE=ON \
        PROVISION_DEPENDENCIES=OFF JPEGXL_BUNDLE_LIBPNG=OFF \
        JPEGXL_ENABLE_BENCHMARK=OFF JPEGXL_ENABLE_BOXES=ON JPEGXL_ENABLE_COVERAGE=OFF \
        JPEGXL_ENABLE_DEVTOOLS=OFF JPEGXL_ENABLE_DOXYGEN=OFF JPEGXL_ENABLE_EXAMPLES=OFF \
        JPEGXL_ENABLE_FUZZERS=OFF JPEGXL_ENABLE_HWY_AVX3=OFF JPEGXL_ENABLE_HWY_AVX3_DL=OFF \
        JPEGXL_ENABLE_HWY_AVX3_SPR=OFF JPEGXL_ENABLE_HWY_AVX3_ZEN4=OFF \
        JPEGXL_ENABLE_JNI=OFF JPEGXL_ENABLE_LTO=OFF JPEGXL_ENABLE_MANPAGES=OFF \
        JPEGXL_ENABLE_OPENEXR=OFF JPEGXL_ENABLE_PLUGINS=OFF JPEGXL_ENABLE_SJPEG=OFF \
        JPEGXL_ENABLE_SKCMS=ON JPEGXL_ENABLE_TCMALLOC=OFF JPEGXL_ENABLE_TOOLS=OFF \
        JPEGXL_ENABLE_TRANSCODE_JPEG=OFF JPEGXL_ENABLE_VIEWERS=OFF \
        JPEGXL_ENABLE_WASM_THREADS=OFF JPEGXL_FORCE_NEON=OFF \
        JPEGXL_FORCE_SYSTEM_BROTLI=OFF JPEGXL_FORCE_SYSTEM_GTEST=OFF \
        JPEGXL_FORCE_SYSTEM_HWY=OFF JPEGXL_FORCE_SYSTEM_LCMS2=OFF JPEGXL_STATIC=OFF \
        JPEGXL_TEST_TOOLS=OFF JPEGXL_WARNINGS_AS_ERRORS=OFF HWY_WARNINGS_ARE_ERRORS=OFF
    set --global shuba_libjxl_effective_dependency_definitions \
        BROTLI_DISABLE_TESTS=ON HWY_ENABLE_CONTRIB=OFF HWY_ENABLE_EXAMPLES=OFF \
        HWY_ENABLE_TESTS=OFF HWY_FORCE_STATIC_LIBS=ON HWY_SYSTEM_GTEST=ON
end

function shuba_libjxl_validate_submodules
    set --local shuba_inventory (mktemp /tmp/shuba-libjxl-submodules.XXXXXX); or return 1
    $shuba_git_path -C $shuba_project_root submodule status --recursive -- third_party/libjxl >$shuba_inventory
    if test $status -ne 0
        rm -f -- $shuba_inventory
        shuba_fail 'could not read the recursive libjxl submodule inventory'
        return 1
    end
    set --global shuba_libjxl_repository_paths
    set --local shuba_root_found false
    while read --local shuba_line
        test -n "$shuba_line"; or continue
        set --local shuba_marker (string sub --start 1 --length 1 -- "$shuba_line")
        set --local shuba_record (string sub --start 2 -- "$shuba_line" | string trim --left)
        set --local shuba_fields (string split ' ' -- $shuba_record)
        if test (count $shuba_fields) -lt 2
            rm -f -- $shuba_inventory
            shuba_fail "could not parse recursive submodule record: $shuba_line"
            return 1
        end
        set --local shuba_commit $shuba_fields[1]
        set --local shuba_path $shuba_fields[2]
        if not string match --regex --quiet '^[0-9a-f]{40,64}$' -- $shuba_commit
            rm -f -- $shuba_inventory
            shuba_fail "could not parse recursive submodule commit: $shuba_line"
            return 1
        end
        if not string match --regex --quiet '^third_party/libjxl(/[A-Za-z0-9._+-]+)*$' -- $shuba_path
            rm -f -- $shuba_inventory
            shuba_fail "unsafe recursive libjxl submodule path: $shuba_path"
            return 1
        end
        if test $shuba_path = third_party/libjxl
            if not test "$shuba_marker" = ' '; and not begin
                    test "$shuba_marker" = +; and test $shuba_commit = $shuba_libjxl_release_commit
                end
                rm -f -- $shuba_inventory
                shuba_fail 'libjxl is absent, conflicted, or not at the required release commit'
                return 1
            end
            if test $shuba_commit != $shuba_libjxl_release_commit
                rm -f -- $shuba_inventory
                shuba_fail "libjxl commit is $shuba_commit, expected $shuba_libjxl_release_commit"
                return 1
            end
            set shuba_root_found true
        else if test "$shuba_marker" != ' '
            rm -f -- $shuba_inventory
            shuba_fail "recursive libjxl dependency is absent, conflicted, or not at its recorded commit: $shuba_path"
            return 1
        end
        set --local shuba_repository $shuba_project_root/$shuba_path
        if not test -d $shuba_repository; or test -L $shuba_repository
            rm -f -- $shuba_inventory
            shuba_fail "initialized recursive repository is unavailable or symbolic-linked: $shuba_path"
            return 1
        end
        set --local shuba_head ($shuba_git_path -C $shuba_repository rev-parse HEAD); or begin
            rm -f -- $shuba_inventory
            return 1
        end
        if test "$shuba_head" != "$shuba_commit"
            rm -f -- $shuba_inventory
            shuba_fail "recursive repository HEAD differs from its recorded commit: $shuba_path"
            return 1
        end
        set --append shuba_libjxl_repository_paths $shuba_path
    end <$shuba_inventory
    rm -f -- $shuba_inventory
    if test $shuba_root_found != true
        shuba_fail 'the pinned third_party/libjxl submodule is not initialized'
        return 1
    end
end

function shuba_libjxl_require_cache_value --argument-names shuba_key shuba_expected
    set --local shuba_cache $shuba_libjxl_build_root/CMakeCache.txt
    set --local shuba_lines (grep --extended-regexp "^$shuba_key:[^=]+=" $shuba_cache)
    if test $status -ne 0; or test (count $shuba_lines) -ne 1
        shuba_fail "libjxl CMake cache does not contain exactly one $shuba_key entry"
        return 1
    end
    set --local shuba_actual (string replace --regex '^[^=]+=' '' -- $shuba_lines[1])
    if test "$shuba_actual" != "$shuba_expected"
        shuba_fail "libjxl CMake cache $shuba_key is $shuba_actual, expected $shuba_expected"
        return 1
    end
end

function shuba_libjxl_validate_outputs
    shuba_require_regular_file $shuba_libjxl_build_root/CMakeCache.txt; or return 1
    for shuba_definition in $shuba_libjxl_cmake_definitions $shuba_libjxl_effective_dependency_definitions
        set --local shuba_fields (string split --max 1 = -- $shuba_definition)
        shuba_libjxl_require_cache_value $shuba_fields[1] $shuba_fields[2]; or return 1
    end
    shuba_libjxl_require_cache_value CMAKE_GENERATOR Ninja; or return 1

    set --local shuba_inspection (mktemp /tmp/shuba-libjxl-readelf.XXXXXX); or return 1
    for shuba_archive in $shuba_libjxl_expected_archives
        set --local shuba_archive_path $shuba_libjxl_build_root/$shuba_archive
        if not test -s $shuba_archive_path; or test -L $shuba_archive_path; or not test -r $shuba_archive_path
            rm -f -- $shuba_inspection
            shuba_fail "required libjxl static archive is missing, empty, unreadable, or symbolic-linked: $shuba_archive"
            return 1
        end
        set --local shuba_members ($shuba_libjxl_llvm_ar t $shuba_archive_path); or begin
            rm -f -- $shuba_inspection
            return 1
        end
        if test (count $shuba_members) -eq 0
            rm -f -- $shuba_inspection
            shuba_fail "libjxl static archive has no members: $shuba_archive"
            return 1
        end
        $shuba_libjxl_llvm_readelf -h $shuba_archive_path >$shuba_inspection; or begin
            rm -f -- $shuba_inspection
            return 1
        end
        set --local shuba_machines (sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p' $shuba_inspection | sort --unique)
        set --local shuba_machine_statuses $pipestatus
        for shuba_status in $shuba_machine_statuses
            if test $shuba_status -ne 0
                rm -f -- $shuba_inspection
                return 1
            end
        end
        if test (count $shuba_machines) -ne 1; or test "$shuba_machines[1]" != AArch64
            rm -f -- $shuba_inspection
            shuba_fail "libjxl static archive is not exclusively AArch64: $shuba_archive"
            return 1
        end
    end
    rm -f -- $shuba_inspection

    set --local shuba_actual_archive_file (mktemp /tmp/shuba-libjxl-archives-actual.XXXXXX); or return 1
    set --local shuba_expected_archive_file (mktemp /tmp/shuba-libjxl-archives-expected.XXXXXX); or begin
        rm -f -- $shuba_actual_archive_file
        return 1
    end
    find -P $shuba_libjxl_build_root -type f -name '*.a' -printf '%P\n' | sort >$shuba_actual_archive_file
    set --local shuba_archive_statuses $pipestatus
    for shuba_status in $shuba_archive_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_actual_archive_file $shuba_expected_archive_file
            return 1
        end
    end
    printf '%s\n' $shuba_libjxl_expected_archives | sort >$shuba_expected_archive_file
    set shuba_archive_statuses $pipestatus
    for shuba_status in $shuba_archive_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_actual_archive_file $shuba_expected_archive_file
            return 1
        end
    end
    cmp --silent $shuba_actual_archive_file $shuba_expected_archive_file
    set --local shuba_archive_inventory_status $status
    rm -f -- $shuba_actual_archive_file $shuba_expected_archive_file
    if test $shuba_archive_inventory_status -ne 0
        shuba_fail "libjxl static archive inventory differs from version $shuba_libjxl_output_inventory_version"
        return 1
    end
    for shuba_header in $shuba_libjxl_expected_headers
        set --local shuba_header_path $shuba_libjxl_build_root/$shuba_header
        if not test -s $shuba_header_path; or test -L $shuba_header_path; or not test -r $shuba_header_path
            shuba_fail "required libjxl public/generated header is unavailable: $shuba_header"
            return 1
        end
    end
    set --local shuba_actual_header_file (mktemp /tmp/shuba-libjxl-headers-actual.XXXXXX); or return 1
    set --local shuba_expected_header_file (mktemp /tmp/shuba-libjxl-headers-expected.XXXXXX); or begin
        rm -f -- $shuba_actual_header_file
        return 1
    end
    find -P $shuba_libjxl_build_root/lib/include/jxl -type f -printf 'lib/include/jxl/%P\n' | sort >$shuba_actual_header_file
    set --local shuba_header_statuses $pipestatus
    for shuba_status in $shuba_header_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_actual_header_file $shuba_expected_header_file
            return 1
        end
    end
    printf '%s\n' $shuba_libjxl_expected_headers | sort >$shuba_expected_header_file
    set shuba_header_statuses $pipestatus
    for shuba_status in $shuba_header_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_actual_header_file $shuba_expected_header_file
            return 1
        end
    end
    cmp --silent $shuba_actual_header_file $shuba_expected_header_file
    set --local shuba_header_inventory_status $status
    rm -f -- $shuba_actual_header_file $shuba_expected_header_file
    if test $shuba_header_inventory_status -ne 0
        shuba_fail "libjxl public/generated header inventory differs from version $shuba_libjxl_output_inventory_version"
        return 1
    end
    set --local shuba_shared_object (find -P $shuba_libjxl_build_root -type f -name '*.so' -print -quit)
    if test $status -ne 0; or test -n "$shuba_shared_object"
        shuba_fail 'libjxl build contains an unexpected shared-library output'
        return 1
    end
end

function shuba_libjxl_write_descriptor --argument-names shuba_output
    set --local shuba_tools $shuba_output.tools
    set --local shuba_sources $shuba_output.sources
    shuba_write_structured_tool_descriptor $shuba_tools; or return 1
    shuba_write_tooling_source_manifest $shuba_sources $shuba_project_root \
        $shuba_project_root/tools/release/build-libjxl-android.fish \
        $shuba_project_root/tools/release/lib/core.fish \
        $shuba_project_root/tools/release/lib/release-contract.fish \
        $shuba_project_root/tools/release/lib/android-toolchain.fish \
        $shuba_project_root/tools/release/lib/repository-state.fish \
        $shuba_project_root/tools/release/lib/fingerprint.fish; or return 1
    begin
        printf '%s\n' 'fingerprint_schema_version=2' \
            "output_inventory_version=$shuba_libjxl_output_inventory_version" \
            "libjxl_release_version=$shuba_libjxl_release_version" \
            "libjxl_release_commit=$shuba_libjxl_release_commit" \
            "android_abi=$shuba_libjxl_android_abi" "android_api=$shuba_libjxl_android_api" \
            'cmake_generator=Ninja' 'build_type=Release'
        for shuba_tool_record in \
            "cmake|$shuba_libjxl_cmake|$shuba_libjxl_cmake_version" \
            "ninja|$shuba_libjxl_ninja|$shuba_libjxl_ninja_version" \
            "git|$shuba_git_path|$shuba_git_version" \
            "clangxx|$shuba_libjxl_clangxx|$shuba_libjxl_clang_version" \
            "llvm_ar|$shuba_libjxl_llvm_ar|unreported" \
            "llvm_readelf|$shuba_libjxl_llvm_readelf|unreported"
            set --local shuba_fields (string split '|' -- $shuba_tool_record)
            set --local shuba_resolved_tool (realpath --canonicalize-existing -- $shuba_fields[2]); or return 1
            printf 'tool.%s.path=%s\ntool.%s.resolved_path=%s\ntool.%s.version=%s\ntool.%s.sha256=%s\n' \
                $shuba_fields[1] $shuba_fields[2] $shuba_fields[1] $shuba_resolved_tool \
                $shuba_fields[1] $shuba_fields[3] $shuba_fields[1] (shuba_sha256_file $shuba_resolved_tool)
        end
        printf 'ndk.version=%s\nndk.source_properties_sha256=%s\n' \
            (shuba_contract_get android.ndk_version) (shuba_sha256_file $shuba_libjxl_ndk_source_properties)
        printf 'ndk.toolchain_path=%s\nndk.toolchain_sha256=%s\n' \
            $shuba_libjxl_toolchain (shuba_sha256_file $shuba_libjxl_toolchain)
        printf 'build_target=%s\n' $shuba_libjxl_build_targets
        printf 'expected_archive=%s\n' $shuba_libjxl_expected_archives
        printf 'expected_header=%s\n' $shuba_libjxl_expected_headers
        printf 'cmake_definition=%s\n' $shuba_libjxl_cmake_definitions
        printf 'effective_dependency_definition=%s\n' $shuba_libjxl_effective_dependency_definitions
        for shuba_repository_path in $shuba_libjxl_repository_paths
            set --local shuba_repository $shuba_project_root/$shuba_repository_path
            set --local shuba_commit ($shuba_git_path -C $shuba_repository rev-parse HEAD); or return 1
            set --local shuba_status ($shuba_git_path -C $shuba_repository status --porcelain=v1 --untracked-files=all --ignore-submodules=none); or return 1
            set --local shuba_dirty false
            test (count $shuba_status) -eq 0; or set shuba_dirty true
            printf 'repository=%s|commit=%s|dirty=%s|state_sha256=%s\n' \
                $shuba_repository_path $shuba_commit $shuba_dirty (shuba_repository_state_digest $shuba_repository)
        end
        cat -- $shuba_tools
        cat -- $shuba_sources
    end >$shuba_output
    set --local shuba_descriptor_status $status
    rm -f -- $shuba_tools $shuba_sources
    return $shuba_descriptor_status
end

function shuba_libjxl_prepare_tools
    set --global shuba_libjxl_android_abi (shuba_contract_get android.abi)
    set --global shuba_libjxl_android_api (shuba_contract_get android.min_sdk)
    if test $shuba_libjxl_android_abi != arm64-v8a
        shuba_fail "libjxl builder owns only arm64-v8a, not $shuba_libjxl_android_abi"
        return 1
    end
    set --local shuba_ndk_root $shuba_android_sdk_root/ndk/(shuba_contract_get android.ndk_version)
    set --local shuba_cmake_root $shuba_android_sdk_root/cmake/(shuba_contract_get android.cmake_version)
    set --global shuba_libjxl_toolchain $shuba_ndk_root/build/cmake/android.toolchain.cmake
    set --global shuba_libjxl_ndk_source_properties $shuba_ndk_root/source.properties
    set --global shuba_libjxl_cmake $shuba_cmake_root/bin/cmake
    set --global shuba_libjxl_ninja $shuba_cmake_root/bin/ninja
    set --local shuba_prebuilt_paths (find -P $shuba_ndk_root/toolchains/llvm/prebuilt -mindepth 1 -maxdepth 1 -type d -print)
    if test $status -ne 0; or test (count $shuba_prebuilt_paths) -ne 1
        shuba_fail 'expected exactly one NDK host LLVM prebuilt directory'
        return 1
    end
    set --local shuba_llvm_bin $shuba_prebuilt_paths[1]/bin
    set --global shuba_libjxl_clangxx $shuba_llvm_bin/clang++
    set --global shuba_libjxl_llvm_ar $shuba_llvm_bin/llvm-ar
    set --global shuba_libjxl_llvm_readelf $shuba_llvm_bin/llvm-readelf
    for shuba_tool in $shuba_libjxl_cmake $shuba_libjxl_ninja \
        $shuba_libjxl_clangxx $shuba_libjxl_llvm_ar $shuba_libjxl_llvm_readelf
        if not test -x $shuba_tool
            shuba_fail "required pinned libjxl build tool is not executable: $shuba_tool"
            return 1
        end
        set --local shuba_resolved (realpath --canonicalize-existing -- $shuba_tool); or return 1
        if not test -f $shuba_resolved; or not test -x $shuba_resolved
            shuba_fail "pinned libjxl build tool resolves to an invalid executable: $shuba_tool"
            return 1
        end
    end
    shuba_require_regular_file $shuba_libjxl_toolchain; or return 1
    shuba_require_regular_file $shuba_libjxl_ndk_source_properties; or return 1
    set --global shuba_git_path (shuba_resolve_command git); or return 1
    set --global shuba_libjxl_cmake_version (shuba_first_line $shuba_libjxl_cmake --version); or return 1
    string match --quiet "cmake version "(shuba_contract_get android.cmake_version)'*' -- $shuba_libjxl_cmake_version; or begin
        shuba_fail "pinned CMake reports an unexpected version: $shuba_libjxl_cmake_version"
        return 1
    end
    set --global shuba_libjxl_ninja_version (shuba_first_line $shuba_libjxl_ninja --version); or return 1
    set --global shuba_libjxl_clang_version (shuba_first_line $shuba_libjxl_clangxx --version); or return 1
    set --global shuba_git_version (shuba_first_line $shuba_git_path --version); or return 1
    set --global shuba_libjxl_cmake_definitions \
        "ANDROID_ABI=$shuba_libjxl_android_abi" "ANDROID_PLATFORM=android-$shuba_libjxl_android_api" \
        CMAKE_BUILD_TYPE=Release "CMAKE_MAKE_PROGRAM=$shuba_libjxl_ninja" \
        "CMAKE_TOOLCHAIN_FILE=$shuba_libjxl_toolchain" $shuba_libjxl_fixed_definitions
end

function shuba_libjxl_build --argument-names shuba_descriptor shuba_fingerprint
    rm -rf -- $shuba_libjxl_build_root; or return 1
    mkdir -p -- $shuba_libjxl_build_root; or return 1
    set --local shuba_arguments --warn-unused-vars -S $shuba_libjxl_source_root \
        -B $shuba_libjxl_build_root -G Ninja
    for shuba_definition in $shuba_libjxl_cmake_definitions
        set --append shuba_arguments "-D$shuba_definition"
    end
    set --local shuba_log $shuba_libjxl_build_root/shuba-cmake-configure.log
    $shuba_libjxl_cmake $shuba_arguments >$shuba_log 2>&1
    set --local shuba_configure_status $status
    cat -- $shuba_log; or return 1
    if test $shuba_configure_status -ne 0
        shuba_fail "libjxl CMake configuration failed with status $shuba_configure_status"
        return 1
    end
    if grep --fixed-strings --quiet 'Manually-specified variables were not used by the project:' $shuba_log
        shuba_fail 'libjxl CMake reported an unused manually specified variable'
        return 1
    end
    set --local shuba_jobs
    if set --query SHUBA_BUILD_JOBS; and test -n "$SHUBA_BUILD_JOBS"
        set shuba_jobs $SHUBA_BUILD_JOBS
    else
        set shuba_jobs (getconf _NPROCESSORS_ONLN 2>/dev/null); or set shuba_jobs 1
    end
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_jobs
        shuba_fail 'SHUBA_BUILD_JOBS must be a positive integer'
        return 1
    end
    $shuba_libjxl_cmake --build $shuba_libjxl_build_root --parallel $shuba_jobs \
        --target $shuba_libjxl_build_targets; or return 1
    shuba_libjxl_validate_outputs; or return 1
    set --global shuba_libjxl_final_descriptor (mktemp $shuba_build_parent/.shuba-libjxl-final.XXXXXX); or return 1
    shuba_libjxl_write_descriptor $shuba_libjxl_final_descriptor; or return 1
    set --local shuba_final_fingerprint (shuba_sha256_file $shuba_libjxl_final_descriptor); or return 1
    if test "$shuba_final_fingerprint" != "$shuba_fingerprint"
        shuba_fail 'libjxl fingerprint inputs changed during the build; no stamp was written'
        return 1
    end
    shuba_write_stamp $shuba_libjxl_stamp 2 $shuba_fingerprint $shuba_descriptor; or return 1
    shuba_stamp_matches $shuba_libjxl_stamp 2 $shuba_fingerprint; or return 1
    printf 'libjxl Android build: built and validated fingerprint %s.\n' $shuba_fingerprint
end

function shuba_libjxl_main
    set --local shuba_mode build
    set --local shuba_lock_held false
    if test (count $argv) -eq 2; and test "$argv[1]" = --lock-held
        set shuba_lock_held true
        set shuba_mode $argv[2]
        contains -- $shuba_mode build clean check print-fingerprint; or return 1
    else if test (count $argv) -gt 1
        shuba_fail 'only one libjxl option may be supplied'
        return 1
    else if test (count $argv) -eq 1
        switch $argv[1]
            case --help
                shuba_libjxl_usage
                return 0
            case --clean --check --print-fingerprint
                set shuba_mode (string replace -- '--' '' $argv[1])
            case '*'
                shuba_libjxl_usage >&2
                shuba_fail "unknown libjxl option: $argv[1]"
                return 1
        end
    end
    set --global shuba_tools_root (realpath --canonicalize-existing -- (status dirname)); or return 1
    set --global shuba_project_root (realpath --canonicalize-existing -- $shuba_tools_root/../..); or return 1
    set --global shuba_libjxl_source_root $shuba_project_root/third_party/libjxl
    set --global shuba_build_parent $shuba_project_root/build
    set --global shuba_libjxl_build_root $shuba_build_parent/libjxl-android-arm64
    set --global shuba_libjxl_stamp $shuba_libjxl_build_root/.shuba-libjxl-build.stamp
    set --local shuba_lock_file $shuba_build_parent/.shuba-libjxl-android-arm64.lock
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_require_fish_version (shuba_contract_get tool.fish_min_version); or return 1
    shuba_validate_owned_directory $shuba_build_parent $shuba_project_root 'owned build parent'; or return 1
    if test $shuba_lock_held != true
        set --local shuba_flock_path (shuba_resolve_command flock); or return 1
        set --local shuba_current_fish (realpath --canonicalize-existing -- (status fish-path)); or return 1
        $shuba_flock_path --exclusive $shuba_lock_file $shuba_current_fish --no-config \
            $shuba_tools_root/build-libjxl-android.fish --lock-held $shuba_mode
        return $status
    end
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_validate_android_sdk_layout; or return 1
    shuba_libjxl_initialize_configuration
    shuba_libjxl_prepare_tools; or return 1
    shuba_require_regular_file $shuba_libjxl_source_root/CMakeLists.txt; or return 1
    if test -L $shuba_libjxl_build_root
        shuba_fail 'owned libjxl build root must not be a symbolic link'
        return 1
    end
    shuba_libjxl_validate_submodules; or return 1
    set --global shuba_libjxl_initial_descriptor (mktemp $shuba_build_parent/.shuba-libjxl-initial.XXXXXX); or return 1
    shuba_libjxl_write_descriptor $shuba_libjxl_initial_descriptor; or return 1
    set --local shuba_fingerprint (shuba_sha256_file $shuba_libjxl_initial_descriptor); or return 1
    if test $shuba_mode = print-fingerprint
        printf '%s\n' $shuba_fingerprint
        return 0
    end
    set --local shuba_cache_valid false
    if shuba_stamp_matches $shuba_libjxl_stamp 2 $shuba_fingerprint
        if shuba_libjxl_validate_outputs
            set shuba_cache_valid true
        end
    end
    if test $shuba_mode = check
        if test $shuba_cache_valid != true
            shuba_fail "cached libjxl tree is absent, stale, or invalid for fingerprint $shuba_fingerprint"
            return 1
        end
        set --global shuba_libjxl_final_descriptor (mktemp $shuba_build_parent/.shuba-libjxl-final.XXXXXX); or return 1
        shuba_libjxl_write_descriptor $shuba_libjxl_final_descriptor; or return 1
        test (shuba_sha256_file $shuba_libjxl_final_descriptor) = $shuba_fingerprint; or begin
            shuba_fail 'libjxl fingerprint inputs changed during cache validation'
            return 1
        end
        printf 'libjxl Android build: validated cached fingerprint %s.\n' $shuba_fingerprint
        return 0
    end
    if test $shuba_mode != clean; and test $shuba_cache_valid = true
        printf 'libjxl Android build: reusing validated fingerprint %s.\n' $shuba_fingerprint
        return 0
    end
    if test -e $shuba_libjxl_build_root
        printf 'libjxl Android build: removing invalidated owned tree %s\n' $shuba_libjxl_build_root
    end
    shuba_libjxl_build $shuba_libjxl_initial_descriptor $shuba_fingerprint
end

set --global shuba_libjxl_initial_descriptor ''
set --global shuba_libjxl_final_descriptor ''
function shuba_libjxl_cleanup
    for shuba_descriptor in $shuba_libjxl_initial_descriptor $shuba_libjxl_final_descriptor
        if test -n "$shuba_descriptor"
            rm -f -- $shuba_descriptor $shuba_descriptor.tools $shuba_descriptor.sources
        end
    end
end
function shuba_libjxl_exit_cleanup --on-event fish_exit
    shuba_libjxl_cleanup
end
function shuba_libjxl_interrupt_cleanup --on-signal INT
    shuba_libjxl_cleanup
    exit 130
end
function shuba_libjxl_terminate_cleanup --on-signal TERM
    shuba_libjxl_cleanup
    exit 143
end

umask 022
set --global --export LC_ALL C
set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/repository-state.fish
source $shuba_script_directory/lib/fingerprint.fish

shuba_libjxl_main $argv
set --local shuba_main_status $status
shuba_libjxl_cleanup
exit $shuba_main_status
