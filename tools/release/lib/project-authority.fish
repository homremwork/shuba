function shuba_project_authority_canonical_sha256 --argument-names shuba_project_file
    shuba_require_regular_file $shuba_project_file; or return 1
    set --local shuba_canonical (mktemp /tmp/shuba-project-authority.XXXXXX); or return 1
    set --local shuba_carriage_return (string unescape '\r')
    sed "s/"$shuba_carriage_return"\$//" $shuba_project_file >$shuba_canonical
    set --local shuba_status $status
    if test $shuba_status -ne 0
        rm -f -- $shuba_canonical
        shuba_fail 'could not normalize project authority line endings'
        return 1
    end
    if grep --fixed-strings --quiet -- $shuba_carriage_return $shuba_canonical
        rm -f -- $shuba_canonical
        shuba_fail 'project authority contains a forbidden lone carriage return'
        return 1
    end
    set --local shuba_digest (shuba_sha256_file $shuba_canonical)
    set shuba_status $status
    rm -f -- $shuba_canonical
    test $shuba_status -eq 0; or return $shuba_status
    printf '%s\n' $shuba_digest
end
