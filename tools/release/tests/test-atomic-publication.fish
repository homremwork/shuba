#!/usr/bin/fish --no-config

function shuba_atomic_test_fail
    printf 'R12F atomic-publication tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_atomic_test_main
    set --global shuba_atomic_test_root (mktemp --directory /tmp/shuba-r12f-atomic.XXXXXX); or return 1
    set --local shuba_parent $shuba_atomic_test_root/output
    set --local shuba_destination $shuba_parent/release
    mkdir $shuba_parent; or return 1

    shuba_atomic_publication_initialize $shuba_parent $shuba_destination test; or return 1
    printf 'first\n' >(shuba_atomic_publication_stage_path)/artifact
    shuba_atomic_publication_publish; or return 1
    set --local shuba_first_value (string trim --right -- (string collect <$shuba_destination/artifact))
    test "$shuba_first_value" = first; or return 1
    shuba_atomic_publication_commit; or return 1

    shuba_atomic_publication_initialize $shuba_parent $shuba_destination test; or return 1
    printf 'replacement\n' >(shuba_atomic_publication_stage_path)/artifact
    shuba_atomic_publication_publish; or return 1
    shuba_atomic_publication_cleanup 99; or return 1
    set --local shuba_rollback_value (string trim --right -- (string collect <$shuba_destination/artifact))
    test "$shuba_rollback_value" = first; or begin
        shuba_atomic_test_fail 'rollback did not restore the prior directory'
        return 1
    end

    shuba_atomic_publication_initialize $shuba_parent $shuba_destination test; or return 1
    printf 'final\n' >(shuba_atomic_publication_stage_path)/artifact
    shuba_atomic_publication_publish; or return 1
    shuba_atomic_publication_commit; or return 1
    shuba_atomic_publication_cleanup 0; or return 1
    set --local shuba_final_value (string trim --right -- (string collect <$shuba_destination/artifact))
    test "$shuba_final_value" = final; or return 1

    printf '%s\n' 'R12F atomic publication new, replacement, commit, and injected rollback probes: passed'
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/atomic-publication.fish

set --global shuba_atomic_test_root ''
function shuba_atomic_test_cleanup --on-event fish_exit
    if test -n "$shuba_atomic_test_root"; and test -d $shuba_atomic_test_root
        rm -rf -- $shuba_atomic_test_root
    end
end

shuba_atomic_test_main
set --local shuba_main_status $status
exit $shuba_main_status
