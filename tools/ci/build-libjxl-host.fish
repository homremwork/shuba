#!/usr/bin/fish --no-config

function shuba_host_jxl_usage
    printf '%s\n' \
        'Usage: tools/ci/build-libjxl-host.fish [OPTION]' \
        '' \
        'Build or validate the fingerprinted libjxl 0.12 host installation.' \
        '' \
        'Options:' \
        '  --clean               rebuild the owned build and install trees' \
        '  --check               validate the current trees without mutation' \
        '  --print-fingerprint   print the current input fingerprint' \
        '  --print-prefix        print the validated installation prefix' \
        '  --help                show this help'
end

function shuba_host_jxl_initialize
    set --global shuba_host_jxl_release_version 0.12.0
    set --global shuba_host_jxl_release_commit a7a9c787341cf703dede03c2009fa460cae5e5df
    set --global shuba_host_jxl_stamp_schema 1
    set --global shuba_host_jxl_output_schema 1
    set --global shuba_host_jxl_definitions \
        BUILD_SHARED_LIBS=OFF \
        BUILD_TESTING=OFF \
        CMAKE_BUILD_TYPE=Release \
        CMAKE_POSITION_INDEPENDENT_CODE=ON \
        JPEGXL_BUNDLE_LIBPNG=OFF \
        JPEGXL_ENABLE_BENCHMARK=OFF \
        JPEGXL_ENABLE_COVERAGE=OFF \
        JPEGXL_ENABLE_DEVTOOLS=OFF \
        JPEGXL_ENABLE_DOXYGEN=OFF \
        JPEGXL_ENABLE_EXAMPLES=OFF \
        JPEGXL_ENABLE_FUZZERS=OFF \
        JPEGXL_ENABLE_JNI=OFF \
        JPEGXL_ENABLE_LTO=OFF \
        JPEGXL_ENABLE_MANPAGES=OFF \
        JPEGXL_ENABLE_OPENEXR=OFF \
        JPEGXL_ENABLE_PLUGINS=OFF \
        JPEGXL_ENABLE_SJPEG=OFF \
        JPEGXL_ENABLE_SKCMS=ON \
        JPEGXL_ENABLE_TCMALLOC=OFF \
        JPEGXL_ENABLE_TOOLS=OFF \
        JPEGXL_ENABLE_TRANSCODE_JPEG=OFF \
        JPEGXL_ENABLE_VIEWERS=OFF \
        JPEGXL_ENABLE_WASM_THREADS=OFF \
        JPEGXL_FORCE_SYSTEM_BROTLI=OFF \
        JPEGXL_FORCE_SYSTEM_GTEST=OFF \
        JPEGXL_FORCE_SYSTEM_HWY=OFF \
        JPEGXL_FORCE_SYSTEM_LCMS2=OFF \
        JPEGXL_STATIC=OFF \
        JPEGXL_TEST_TOOLS=OFF \
        JPEGXL_WARNINGS_AS_ERRORS=OFF \
        PROVISION_DEPENDENCIES=OFF \
        HWY_ENABLE_CONTRIB=OFF \
        HWY_ENABLE_EXAMPLES=OFF \
        HWY_ENABLE_TESTS=OFF \
        HWY_FORCE_STATIC_LIBS=ON \
        HWY_SYSTEM_GTEST=ON
end

function shuba_host_jxl_validate_sources
    set --local shuba_inventory (mktemp /tmp/shuba-host-jxl-submodules.XXXXXX); or return 1
    $shuba_host_jxl_git -C $shuba_host_jxl_project_root submodule status --recursive \
        -- third_party/libjxl >$shuba_inventory
    if test $status -ne 0
        rm -f -- $shuba_inventory
        shuba_fail 'could not read the recursive host-libjxl source graph'
        return 1
    end
    set --global shuba_host_jxl_repositories
    set --local shuba_root_found false
    while read --local shuba_line
        test -n "$shuba_line"; or continue
        set --local shuba_marker (string sub --start 1 --length 1 -- "$shuba_line")
        set --local shuba_fields (string split ' ' -- (string sub --start 2 -- "$shuba_line" | string trim --left))
        if test (count $shuba_fields) -lt 2
            rm -f -- $shuba_inventory
            shuba_fail "could not parse host-libjxl source record: $shuba_line"
            return 1
        end
        set --local shuba_commit $shuba_fields[1]
        set --local shuba_path $shuba_fields[2]
        if test "$shuba_marker" != ' '
            rm -f -- $shuba_inventory
            shuba_fail "host-libjxl source is absent, conflicted, or not at its recorded commit: $shuba_path"
            return 1
        end
        if not string match --regex --quiet '^third_party/libjxl(/[A-Za-z0-9._+-]+)*$' -- $shuba_path
            rm -f -- $shuba_inventory
            shuba_fail "host-libjxl source path is unsafe: $shuba_path"
            return 1
        end
        if not string match --regex --quiet '^[0-9a-f]{40,64}$' -- $shuba_commit
            rm -f -- $shuba_inventory
            shuba_fail "host-libjxl source commit is malformed: $shuba_path"
            return 1
        end
        set --local shuba_repository $shuba_host_jxl_project_root/$shuba_path
        if not test -d $shuba_repository; or test -L $shuba_repository
            rm -f -- $shuba_inventory
            shuba_fail "host-libjxl source directory is unavailable: $shuba_path"
            return 1
        end
        if test ($shuba_host_jxl_git -C $shuba_repository rev-parse HEAD) != $shuba_commit
            rm -f -- $shuba_inventory
            shuba_fail "host-libjxl source HEAD differs from its recorded commit: $shuba_path"
            return 1
        end
        if test $shuba_path = third_party/libjxl
            if test $shuba_commit != $shuba_host_jxl_release_commit
                rm -f -- $shuba_inventory
                shuba_fail 'host libjxl is not at the required v0.12.0 commit'
                return 1
            end
            set shuba_root_found true
        end
        set --append shuba_host_jxl_repositories $shuba_path
    end <$shuba_inventory
    rm -f -- $shuba_inventory
    if test $shuba_root_found != true
        shuba_fail 'the pinned host-libjxl source is not initialized'
        return 1
    end
end

function shuba_host_jxl_write_descriptor --argument-names shuba_output
    set --local shuba_sources $shuba_output.sources
    shuba_write_tooling_source_manifest $shuba_sources $shuba_host_jxl_project_root \
        $shuba_host_jxl_project_root/tools/ci/build-libjxl-host.fish \
        $shuba_host_jxl_project_root/tools/release/lib/core.fish \
        $shuba_host_jxl_project_root/tools/release/lib/repository-state.fish \
        $shuba_host_jxl_project_root/tools/release/lib/fingerprint.fish; or return 1
    begin
        printf '%s\n' \
            'fingerprint_schema_version=1' \
            "output_schema_version=$shuba_host_jxl_output_schema" \
            "libjxl_release_version=$shuba_host_jxl_release_version" \
            "libjxl_release_commit=$shuba_host_jxl_release_commit" \
            'cmake_generator=Ninja' \
            'build_type=Release'
        for shuba_record in \
            "cmake|$shuba_host_jxl_cmake|$shuba_host_jxl_cmake_version" \
            "ninja|$shuba_host_jxl_ninja|$shuba_host_jxl_ninja_version" \
            "cc|$shuba_host_jxl_cc|$shuba_host_jxl_cc_version" \
            "cxx|$shuba_host_jxl_cxx|$shuba_host_jxl_cxx_version" \
            "git|$shuba_host_jxl_git|$shuba_host_jxl_git_version" \
            "pkg_config|$shuba_host_jxl_pkg_config|$shuba_host_jxl_pkg_config_version"
            set --local shuba_fields (string split '|' -- $shuba_record)
            printf 'tool.%s.path=%s\ntool.%s.version=%s\ntool.%s.sha256=%s\n' \
                $shuba_fields[1] $shuba_fields[2] $shuba_fields[1] $shuba_fields[3] \
                $shuba_fields[1] (shuba_sha256_file $shuba_fields[2])
        end
        printf 'cmake_definition=%s\n' $shuba_host_jxl_definitions
        for shuba_path in $shuba_host_jxl_repositories
            set --local shuba_repository $shuba_host_jxl_project_root/$shuba_path
            set --local shuba_commit ($shuba_host_jxl_git -C $shuba_repository rev-parse HEAD); or return 1
            set --local shuba_status ($shuba_host_jxl_git -C $shuba_repository status \
                --porcelain=v1 --untracked-files=all --ignore-submodules=none); or return 1
            set --local shuba_dirty false
            test (count $shuba_status) -eq 0; or set shuba_dirty true
            printf 'repository=%s|commit=%s|dirty=%s|state_sha256=%s\n' \
                $shuba_path $shuba_commit $shuba_dirty (shuba_repository_state_digest $shuba_repository)
        end
        cat -- $shuba_sources
    end >$shuba_output
    set --local shuba_status $status
    rm -f -- $shuba_sources
    return $shuba_status
end

function shuba_host_jxl_validate_outputs
    for shuba_path in \
        include/jxl/decode.h include/jxl/encode.h include/jxl/resizable_parallel_runner.h \
        include/jxl/thread_parallel_runner.h lib/libjxl.a lib/libjxl_cms.a \
        lib/libjxl_threads.a lib/pkgconfig/libjxl.pc lib/pkgconfig/libjxl_cms.pc \
        lib/pkgconfig/libjxl_threads.pc
        shuba_require_regular_file $shuba_host_jxl_prefix/$shuba_path; or return 1
    end
    set --local shuba_unexpected_shared (find -P $shuba_host_jxl_prefix -type f -name '*.so*' -print -quit)
    if test -n "$shuba_unexpected_shared"
        shuba_fail "host-libjxl prefix contains a shared library: $shuba_unexpected_shared"
        return 1
    end
    set --local shuba_prefix (realpath --canonicalize-existing -- $shuba_host_jxl_prefix); or return 1
    set --local shuba_pc_path $shuba_prefix/lib/pkgconfig
    set --local shuba_version (env PKG_CONFIG_PATH=$shuba_pc_path PKG_CONFIG_LIBDIR=$shuba_pc_path \
        $shuba_host_jxl_pkg_config --modversion libjxl libjxl_threads); or return 1
    if test (count $shuba_version) -ne 2; or test $shuba_version[1] != $shuba_host_jxl_release_version; or test $shuba_version[2] != $shuba_host_jxl_release_version
        shuba_fail 'host-libjxl pkg-config versions differ from v0.12.0'
        return 1
    end
    set --local shuba_pc_prefix (env PKG_CONFIG_PATH=$shuba_pc_path PKG_CONFIG_LIBDIR=$shuba_pc_path \
        $shuba_host_jxl_pkg_config --variable=prefix libjxl); or return 1
    if test "$shuba_pc_prefix" != "$shuba_prefix"
        shuba_fail 'host-libjxl pkg-config prefix is stale or relocated'
        return 1
    end
    set --local shuba_smoke_root (mktemp --directory /tmp/shuba-host-jxl-smoke.XXXXXX); or return 1
    printf '%s\n' '#include <jxl/decode.h>' 'int main() { return JxlDecoderVersion() == 12000 ? 0 : 1; }' >$shuba_smoke_root/main.cpp
    set --local shuba_flag_line (env PKG_CONFIG_PATH=$shuba_pc_path PKG_CONFIG_LIBDIR=$shuba_pc_path \
        $shuba_host_jxl_pkg_config --static --cflags --libs libjxl libjxl_threads | string collect); or begin
        rm -rf -- $shuba_smoke_root
        return 1
    end
    set --local shuba_flags (string split ' ' -- (string trim -- $shuba_flag_line))
    $shuba_host_jxl_cxx -std=c++23 $shuba_smoke_root/main.cpp $shuba_flags -o $shuba_smoke_root/smoke
    and $shuba_smoke_root/smoke
    set --local shuba_smoke_status $status
    rm -rf -- $shuba_smoke_root
    if test $shuba_smoke_status -ne 0
        shuba_fail 'host-libjxl compile/link/runtime smoke probe failed'
        return 1
    end
end

function shuba_host_jxl_build --argument-names shuba_descriptor shuba_fingerprint
    rm -rf -- $shuba_host_jxl_build_root $shuba_host_jxl_prefix; or return 1
    mkdir -p -- $shuba_host_jxl_build_root $shuba_host_jxl_prefix; or return 1
    set --local shuba_arguments --warn-unused-vars -S $shuba_host_jxl_source_root \
        -B $shuba_host_jxl_build_root -G Ninja \
        "-DCMAKE_C_COMPILER=$shuba_host_jxl_cc" \
        "-DCMAKE_CXX_COMPILER=$shuba_host_jxl_cxx" \
        "-DCMAKE_MAKE_PROGRAM=$shuba_host_jxl_ninja" \
        "-DCMAKE_INSTALL_PREFIX=$shuba_host_jxl_prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib
    for shuba_definition in $shuba_host_jxl_definitions
        set --append shuba_arguments "-D$shuba_definition"
    end
    set --local shuba_log $shuba_host_jxl_build_root/shuba-cmake-configure.log
    $shuba_host_jxl_cmake $shuba_arguments >$shuba_log 2>&1
    set --local shuba_configure_status $status
    cat -- $shuba_log; or return 1
    if test $shuba_configure_status -ne 0
        shuba_fail "host-libjxl configuration failed with status $shuba_configure_status"
        return 1
    end
    if grep --fixed-strings --quiet 'Manually-specified variables were not used by the project:' $shuba_log
        shuba_fail 'host-libjxl configuration ignored a manually specified variable'
        return 1
    end
    set --local shuba_jobs (set --query SHUBA_BUILD_JOBS; and echo $SHUBA_BUILD_JOBS; or getconf _NPROCESSORS_ONLN)
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_jobs
        shuba_fail 'SHUBA_BUILD_JOBS must be a positive integer'
        return 1
    end
    $shuba_host_jxl_cmake --build $shuba_host_jxl_build_root --parallel $shuba_jobs \
        --target jxl jxl_cms jxl_threads; or return 1
    $shuba_host_jxl_cmake --install $shuba_host_jxl_build_root; or return 1
    shuba_host_jxl_validate_outputs; or return 1
    set --local shuba_final_descriptor (mktemp $shuba_host_jxl_parent/.shuba-host-jxl-final.XXXXXX); or return 1
    shuba_host_jxl_write_descriptor $shuba_final_descriptor; or return 1
    if test (shuba_sha256_file $shuba_final_descriptor) != $shuba_fingerprint
        rm -f -- $shuba_final_descriptor
        shuba_fail 'host-libjxl fingerprint inputs changed during the build'
        return 1
    end
    shuba_write_stamp $shuba_host_jxl_stamp $shuba_host_jxl_stamp_schema \
        $shuba_fingerprint $shuba_descriptor; or return 1
    rm -f -- $shuba_final_descriptor
    printf 'host libjxl build: built and validated fingerprint %s\n' $shuba_fingerprint
end

function shuba_host_jxl_main
    set --local shuba_mode build
    if test (count $argv) -gt 1
        shuba_host_jxl_usage >&2
        shuba_fail 'only one host-libjxl option may be supplied'
        return 1
    else if test (count $argv) -eq 1
        switch $argv[1]
            case --help
                shuba_host_jxl_usage
                return 0
            case --clean --check --print-fingerprint --print-prefix
                set shuba_mode (string replace -- '--' '' $argv[1])
            case '*'
                shuba_host_jxl_usage >&2
                shuba_fail "unknown host-libjxl option: $argv[1]"
                return 1
        end
    end
    set --global shuba_host_jxl_project_root \
        (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    set --global shuba_host_jxl_source_root $shuba_host_jxl_project_root/third_party/libjxl
    set --global shuba_host_jxl_parent $shuba_host_jxl_project_root/build/ci
    set --global shuba_host_jxl_build_root $shuba_host_jxl_parent/libjxl-host-build
    set --global shuba_host_jxl_prefix $shuba_host_jxl_parent/libjxl-host-prefix
    set --global shuba_host_jxl_stamp $shuba_host_jxl_prefix/.shuba-host-libjxl.stamp
    shuba_validate_owned_directory $shuba_host_jxl_parent $shuba_host_jxl_project_root/build \
        'host CI build parent'; or return 1
    shuba_host_jxl_initialize
    set --global shuba_host_jxl_cmake (shuba_resolve_command cmake); or return 1
    set --global shuba_host_jxl_ninja (shuba_resolve_command ninja); or return 1
    set --global shuba_host_jxl_git (shuba_resolve_command git); or return 1
    set --global shuba_host_jxl_pkg_config (shuba_resolve_command pkg-config); or return 1
    set --global shuba_host_jxl_cc (shuba_resolve_command (set --query CC; and echo $CC; or echo gcc)); or return 1
    set --global shuba_host_jxl_cxx (shuba_resolve_command (set --query CXX; and echo $CXX; or echo g++)); or return 1
    set --global shuba_host_jxl_cmake_version (shuba_first_line $shuba_host_jxl_cmake --version); or return 1
    set --global shuba_host_jxl_ninja_version (shuba_first_line $shuba_host_jxl_ninja --version); or return 1
    set --global shuba_host_jxl_git_version (shuba_first_line $shuba_host_jxl_git --version); or return 1
    set --global shuba_host_jxl_pkg_config_version (shuba_first_line $shuba_host_jxl_pkg_config --version); or return 1
    set --global shuba_host_jxl_cc_version (shuba_first_line $shuba_host_jxl_cc --version); or return 1
    set --global shuba_host_jxl_cxx_version (shuba_first_line $shuba_host_jxl_cxx --version); or return 1
    shuba_host_jxl_validate_sources; or return 1
    set --local shuba_descriptor (mktemp $shuba_host_jxl_parent/.shuba-host-jxl-inputs.XXXXXX); or return 1
    shuba_host_jxl_write_descriptor $shuba_descriptor; or return 1
    set --local shuba_fingerprint (shuba_sha256_file $shuba_descriptor); or return 1
    if test $shuba_mode = print-fingerprint
        rm -f -- $shuba_descriptor
        printf '%s\n' $shuba_fingerprint
        return 0
    end
    set --local shuba_valid false
    if shuba_stamp_matches $shuba_host_jxl_stamp $shuba_host_jxl_stamp_schema $shuba_fingerprint
        if shuba_host_jxl_validate_outputs
            set shuba_valid true
        end
    end
    if contains -- $shuba_mode check print-prefix
        rm -f -- $shuba_descriptor
        if test $shuba_valid != true
            shuba_fail "host-libjxl cache is absent, stale, or invalid for fingerprint $shuba_fingerprint"
            return 1
        end
        if test $shuba_mode = print-prefix
            realpath --canonicalize-existing -- $shuba_host_jxl_prefix
        else
            printf 'host libjxl build: validated cached fingerprint %s\n' $shuba_fingerprint
        end
        return 0
    end
    if test $shuba_mode != clean; and test $shuba_valid = true
        rm -f -- $shuba_descriptor
        printf 'host libjxl build: reusing validated fingerprint %s\n' $shuba_fingerprint
        return 0
    end
    shuba_host_jxl_build $shuba_descriptor $shuba_fingerprint
    set --local shuba_status $status
    rm -f -- $shuba_descriptor
    return $shuba_status
end

umask 022
set --global --export LC_ALL C
set --local shuba_ci_directory (status dirname)
source $shuba_ci_directory/../release/lib/core.fish
source $shuba_ci_directory/../release/lib/repository-state.fish
source $shuba_ci_directory/../release/lib/fingerprint.fish

shuba_host_jxl_main $argv
exit $status
