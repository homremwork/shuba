#!/usr/bin/fish --no-config

function shuba_ci_release_tests_fail
    printf 'CI release-tool tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_ci_release_tests_main
    set --local shuba_mode hermetic
    if test (count $argv) -gt 1
        shuba_ci_release_tests_fail 'usage: run-release-tool-tests.fish [--hermetic|--generated|--rehearsal]'
        return 1
    else if test (count $argv) -eq 1
        switch $argv[1]
            case --hermetic
                set shuba_mode hermetic
            case --generated
                set shuba_mode generated
            case --rehearsal
                set shuba_mode rehearsal
            case '*'
                shuba_ci_release_tests_fail "unknown mode: $argv[1]"
                return 1
        end
    end
    set --local shuba_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    set --local shuba_release_root $shuba_root/tools/release
    set --local shuba_tests_root $shuba_release_root/tests
    set --local shuba_expected_tests \
        test-agp-metadata.fish test-apk-validation.fish test-atomic-publication.fish \
        test-foundations.fish test-generated-android.fish test-release-provenance.fish \
        test-signing-preparer.fish test-upgrade-probe.fish
    set --local shuba_actual_tests (find -P $shuba_tests_root -maxdepth 1 -type f -name 'test-*.fish' -printf '%f\n' | sort)
    if test (string join , -- (printf '%s\n' $shuba_expected_tests | sort)) != (string join , -- $shuba_actual_tests)
        shuba_ci_release_tests_fail 'release-tool test inventory changed without updating the CI entrypoint'
        return 1
    end
    set --local shuba_fish_files (find -P $shuba_release_root $shuba_root/tools/ci -type f -name '*.fish' | sort)
    for shuba_file in $shuba_fish_files
        fish --no-config --no-execute $shuba_file; or return 1
        fish_indent --check $shuba_file; or return 1
    end
    if contains -- $shuba_mode hermetic rehearsal
        for shuba_test in \
            test-agp-metadata.fish test-apk-validation.fish test-atomic-publication.fish \
            test-foundations.fish test-release-provenance.fish test-signing-preparer.fish \
            test-upgrade-probe.fish
            printf 'CI release-tool tests: running %s\n' $shuba_test
            $shuba_tests_root/$shuba_test; or return 1
        end
    end
    if contains -- $shuba_mode generated rehearsal
        printf '%s\n' 'CI release-tool tests: running generated Android mutation corpus'
        $shuba_tests_root/test-generated-android.fish; or return 1
    end
    if test $shuba_mode = rehearsal
        printf '%s\n' 'CI release-tool tests: validating the real signed APK'
        $shuba_tests_root/test-foundations.fish --with-android-toolchain; or return 1
        $shuba_tests_root/test-apk-validation.fish --with-real-apk; or return 1
    end
    printf 'CI release-tool tests: %s phase passed.\n' $shuba_mode
end

shuba_ci_release_tests_main $argv
exit $status
