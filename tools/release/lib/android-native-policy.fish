function shuba_android_native_policy_fail --argument-names shuba_probe_root
    set --erase argv[1]
    rm -rf -- $shuba_probe_root
    shuba_fail $argv
end

function shuba_android_native_policy_prebuilt --argument-names shuba_ndk_root
    set --local shuba_prebuilt_paths \
        (find -P $shuba_ndk_root/toolchains/llvm/prebuilt -mindepth 1 -maxdepth 1 -type d -print)
    if test $status -ne 0; or test (count $shuba_prebuilt_paths) -ne 1
        shuba_fail 'expected exactly one pinned NDK host LLVM prebuilt directory'
        return 1
    end
    realpath --canonicalize-existing -- $shuba_prebuilt_paths[1]
end

function shuba_validate_android_native_policy
    if not set --query shuba_android_sdk_root
        set --global shuba_android_sdk_root (shuba_resolve_android_sdk_root); or return 1
    end

    set --local shuba_cpu (shuba_contract_get android.native_cpu); or return 1
    set --local shuba_feature_floor (shuba_contract_get android.native_feature_floor); or return 1
    set --local shuba_optimization (shuba_contract_get android.release_optimization); or return 1
    set --local shuba_api (shuba_contract_get android.min_sdk); or return 1
    set --local shuba_ndk_root \
        $shuba_android_sdk_root/ndk/(shuba_contract_get android.ndk_version)
    set --local shuba_prebuilt (shuba_android_native_policy_prebuilt $shuba_ndk_root); or return 1
    set --local shuba_llvm_bin $shuba_prebuilt/bin
    set --local shuba_clangxx (realpath --canonicalize-existing -- $shuba_llvm_bin/clang++); or return 1
    set --local shuba_llvm_ar (realpath --canonicalize-existing -- $shuba_llvm_bin/llvm-ar); or return 1
    set --local shuba_llvm_readelf (realpath --canonicalize-existing -- $shuba_llvm_bin/llvm-readelf); or return 1
    for shuba_tool in $shuba_clangxx $shuba_llvm_ar $shuba_llvm_readelf
        shuba_require_executable_file $shuba_tool; or return 1
    end

    set --local shuba_target aarch64-linux-android$shuba_api
    set --local shuba_sysroot $shuba_prebuilt/sysroot
    if not test -d $shuba_sysroot; or test -L $shuba_sysroot
        shuba_fail 'pinned NDK sysroot is unavailable or symbolic-linked'
        return 1
    end
    set --local shuba_flags "-mcpu=$shuba_cpu" "-$shuba_optimization"

    set --local shuba_probe_root \
        (mktemp --directory /tmp/shuba-android-native-policy.XXXXXX); or return 1
    set --local shuba_source $shuba_probe_root/policy.cpp
    printf '%s\n' \
        'extern "C" int shuba_android_native_policy_probe(int value)' \
        '{' \
        '    return value * 17 + 3;' \
        '}' >$shuba_source; or begin
        shuba_android_native_policy_fail $shuba_probe_root \
            'could not write the Android native-policy probe source'
        return 1
    end

    $shuba_clangxx --target=$shuba_target "-mcpu=$shuba_cpu" -### \
        -c -x c++ $shuba_source >$shuba_probe_root/driver.stdout 2>$shuba_probe_root/driver.stderr
    if test $status -ne 0; or not grep --fixed-strings --quiet \
            '"-target-cpu" "cortex-a73"' $shuba_probe_root/driver.stderr
        shuba_android_native_policy_fail $shuba_probe_root \
            'pinned NDK Clang does not lower android.native_cpu to cortex-a73'
        return 1
    end

    $shuba_clangxx --target=$shuba_target "-mcpu=$shuba_cpu" \
        -dM -E -x c++ /dev/null >$shuba_probe_root/macros 2>$shuba_probe_root/macros.diagnostic
    if test $status -ne 0
        shuba_android_native_policy_fail $shuba_probe_root \
            'pinned NDK Clang feature-macro probe failed'
        return 1
    end
    for shuba_macro in \
        '#define __aarch64__ 1' \
        '#define __ARM_ARCH 8' \
        '#define __ARM_NEON 1' \
        '#define __ARM_FEATURE_AES 1' \
        '#define __ARM_FEATURE_SHA2 1' \
        '#define __ARM_FEATURE_CRC32 1'
        if not grep --fixed-strings --line-regexp --quiet -- $shuba_macro $shuba_probe_root/macros
            shuba_android_native_policy_fail $shuba_probe_root \
                "pinned NDK Clang lacks required native feature macro: $shuba_macro"
            return 1
        end
    end
    if grep --fixed-strings --quiet '#define __FAST_MATH__' $shuba_probe_root/macros
        shuba_android_native_policy_fail $shuba_probe_root \
            'Android native policy unexpectedly enables fast-math semantics'
        return 1
    end

    $shuba_clangxx --target=$shuba_target --sysroot=$shuba_sysroot \
        $shuba_flags -fPIC -c $shuba_source -o $shuba_probe_root/policy.o >$shuba_probe_root/compile.stdout 2>$shuba_probe_root/compile.stderr
    if test $status -ne 0
        shuba_android_native_policy_fail $shuba_probe_root \
            'pinned NDK Clang could not compile the selected Android native policy'
        return 1
    end
    $shuba_llvm_ar rcs $shuba_probe_root/libpolicy.a $shuba_probe_root/policy.o >$shuba_probe_root/archive.stdout 2>$shuba_probe_root/archive.stderr
    if test $status -ne 0; or test ($shuba_llvm_ar t $shuba_probe_root/libpolicy.a | string collect) != policy.o
        shuba_android_native_policy_fail $shuba_probe_root \
            'pinned NDK llvm-ar could not preserve the native-policy object'
        return 1
    end
    $shuba_clangxx --target=$shuba_target --sysroot=$shuba_sysroot \
        $shuba_flags -fPIC -shared -nostdlib \
        -Wl,--whole-archive $shuba_probe_root/libpolicy.a -Wl,--no-whole-archive \
        -o $shuba_probe_root/libpolicy.so >$shuba_probe_root/link.stdout 2>$shuba_probe_root/link.stderr
    if test $status -ne 0
        shuba_android_native_policy_fail $shuba_probe_root \
            'pinned NDK Clang could not perform the selected final shared link'
        return 1
    end
    $shuba_llvm_readelf --file-header $shuba_probe_root/libpolicy.so >$shuba_probe_root/elf-header 2>$shuba_probe_root/elf-header.diagnostic
    if test $status -ne 0
        shuba_android_native_policy_fail $shuba_probe_root \
            'pinned NDK llvm-readelf could not inspect the final-link probe'
        return 1
    end
    if not grep --extended-regexp --quiet \
            '^[[:space:]]*Machine:[[:space:]]+EM_AARCH64[[:space:]]+[(]0xB7[)]$' $shuba_probe_root/elf-header
        shuba_android_native_policy_fail $shuba_probe_root \
            'Android native-policy final-link probe is not AArch64 ELF'
        return 1
    end

    rm -rf -- $shuba_probe_root
    printf 'Android native policy check: CPU %s, feature floor %s, and Release -%s passed compile/archive/shared-link probes.\n' \
        $shuba_cpu $shuba_feature_floor $shuba_optimization
end
