function shuba_write_tooling_source_manifest --argument-names shuba_manifest_path shuba_project_root
    set --erase argv[1..2]
    set --local shuba_canonical_root (realpath --canonicalize-existing -- $shuba_project_root); or return 1
    if not test -d $shuba_canonical_root; or test -L $shuba_project_root
        shuba_fail "tooling source manifest root is unavailable or symbolic-linked: $shuba_project_root"
        return 1
    end
    set --local shuba_rows
    set --local shuba_relative_paths
    for shuba_source_path in $argv
        shuba_require_regular_file $shuba_source_path; or return 1
        set --local shuba_canonical_source (realpath --canonicalize-existing -- $shuba_source_path); or return 1
        if not string match --quiet "$shuba_canonical_root/*" -- $shuba_canonical_source
            shuba_fail "tooling source lies outside the workspace: $shuba_source_path"
            return 1
        end
        set --local shuba_relative_path (string replace "$shuba_canonical_root/" '' -- $shuba_canonical_source | string collect)
        if string match --regex --quiet '[[:cntrl:]|]' -- $shuba_relative_path
            shuba_fail 'tooling source manifest rejects control characters and separators in paths'
            return 1
        end
        if contains -- $shuba_relative_path $shuba_relative_paths
            shuba_fail "tooling source manifest repeats a path: $shuba_relative_path"
            return 1
        end
        set --append shuba_relative_paths $shuba_relative_path
        set --append shuba_rows "source|"(stat --format %a -- $shuba_canonical_source)"|"(shuba_sha256_file $shuba_canonical_source)"|$shuba_relative_path"
    end
    if test (count $shuba_rows) -eq 0
        shuba_fail 'tooling source manifest must contain at least one source or query file'
        return 1
    end
    begin
        printf 'tooling_source_manifest_schema_version=1\n'
        printf '%s\n' $shuba_rows | sort
    end >$shuba_manifest_path
end

function shuba_read_stamp_fingerprint --argument-names shuba_stamp_path shuba_expected_schema
    shuba_require_regular_file $shuba_stamp_path; or return 1
    set --local shuba_schema_lines (string match 'stamp_schema_version=*' < $shuba_stamp_path)
    set --local shuba_fingerprint_lines (string match 'fingerprint_sha256=*' < $shuba_stamp_path)
    if test (count $shuba_schema_lines) -ne 1; or test $shuba_schema_lines[1] != "stamp_schema_version=$shuba_expected_schema"
        shuba_fail "stamp schema is absent, duplicated, or stale: $shuba_stamp_path"
        return 1
    end
    if test (count $shuba_fingerprint_lines) -ne 1
        shuba_fail "stamp fingerprint is absent or duplicated: $shuba_stamp_path"
        return 1
    end
    set --local shuba_fingerprint (string replace 'fingerprint_sha256=' '' -- $shuba_fingerprint_lines[1])
    if not string match --regex --quiet '^[0-9a-f]{64}$' -- $shuba_fingerprint
        shuba_fail "stamp fingerprint is malformed: $shuba_stamp_path"
        return 1
    end
    printf '%s\n' $shuba_fingerprint
end

function shuba_stamp_matches --argument-names shuba_stamp_path shuba_expected_schema shuba_expected_fingerprint
    set --local shuba_actual_fingerprint (shuba_read_stamp_fingerprint $shuba_stamp_path $shuba_expected_schema 2>/dev/null); or return 1
    test "$shuba_actual_fingerprint" = "$shuba_expected_fingerprint"
end

function shuba_write_stamp --argument-names shuba_stamp_path shuba_schema shuba_fingerprint shuba_descriptor_path
    shuba_require_regular_file $shuba_descriptor_path; or return 1
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_schema
        shuba_fail 'stamp schema must be a positive integer'
        return 1
    end
    if not string match --regex --quiet '^[0-9a-f]{64}$' -- $shuba_fingerprint
        shuba_fail 'stamp fingerprint must be a SHA-256 digest'
        return 1
    end
    set --local shuba_stamp_parent (path dirname $shuba_stamp_path)
    if not test -d $shuba_stamp_parent; or test -L $shuba_stamp_parent
        shuba_fail "stamp parent is unavailable or symbolic-linked: $shuba_stamp_parent"
        return 1
    end
    set --local shuba_temporary_stamp (mktemp $shuba_stamp_parent/.shuba-stamp.XXXXXX); or return 1
    begin
        printf 'stamp_schema_version=%s\n' $shuba_schema
        printf 'fingerprint_sha256=%s\n' $shuba_fingerprint
        printf '%s\n' fingerprint_inputs_begin
        cat -- $shuba_descriptor_path
        printf '%s\n' fingerprint_inputs_end
    end >$shuba_temporary_stamp
    set --local shuba_write_status $status
    if test $shuba_write_status -eq 0
        chmod 0644 $shuba_temporary_stamp
        and mv --force -- $shuba_temporary_stamp $shuba_stamp_path
        set shuba_write_status $status
    end
    if test $shuba_write_status -ne 0
        rm -f -- $shuba_temporary_stamp
        return $shuba_write_status
    end
end
