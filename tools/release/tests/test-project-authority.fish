#!/usr/bin/fish --no-config

function shuba_project_authority_test_fail
    printf 'Project authority tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_project_authority_test_main
    set --global shuba_project_authority_test_root \
        (mktemp --directory /tmp/shuba-project-authority-tests.XXXXXX); or return 1
    set --local shuba_lf $shuba_project_authority_test_root/lf.jucer
    set --local shuba_crlf $shuba_project_authority_test_root/crlf.jucer
    set --local shuba_mutated $shuba_project_authority_test_root/mutated.jucer
    set --local shuba_lone_cr $shuba_project_authority_test_root/lone-cr.jucer
    printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<JUCERPROJECT version="1.0.2"/>' >$shuba_lf
    sed 's/$/\r/' $shuba_lf >$shuba_crlf; or return 1
    printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<JUCERPROJECT version="1.0.3"/>' >$shuba_mutated
    printf '<?xml version="1.0"?>\r<JUCERPROJECT/>\n' >$shuba_lone_cr

    set --local shuba_lf_digest (shuba_project_authority_canonical_sha256 $shuba_lf); or return 1
    set --local shuba_crlf_digest (shuba_project_authority_canonical_sha256 $shuba_crlf); or return 1
    if test "$shuba_lf_digest" != "$shuba_crlf_digest"
        shuba_project_authority_test_fail 'LF and CRLF serializations produced different authority digests'
        return 1
    end
    set --local shuba_mutated_digest \
        (shuba_project_authority_canonical_sha256 $shuba_mutated); or return 1
    if test "$shuba_lf_digest" = "$shuba_mutated_digest"
        shuba_project_authority_test_fail 'semantic/text mutation preserved the authority digest'
        return 1
    end
    set --local shuba_diagnostic \
        (shuba_project_authority_canonical_sha256 $shuba_lone_cr 2>&1)
    if test $status -eq 0
        shuba_project_authority_test_fail 'lone carriage return was accepted'
        return 1
    end
    if not string match --quiet '*forbidden lone carriage return*' -- \
            (string join \n -- $shuba_diagnostic)
        shuba_project_authority_test_fail 'lone carriage return produced the wrong diagnostic'
        return 1
    end

    printf '%s\n' 'Project authority LF/CRLF equivalence and mutation tests: passed'
end

set --global shuba_project_authority_test_root ''
function shuba_project_authority_test_cleanup --on-event fish_exit
    if test -n "$shuba_project_authority_test_root"; and test -d $shuba_project_authority_test_root
        rm -rf -- $shuba_project_authority_test_root
    end
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/project-authority.fish

shuba_project_authority_test_main
exit $status
