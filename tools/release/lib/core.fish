function shuba_error
    printf 'release tooling: %s\n' (string join ' ' -- $argv) >&2
end

function shuba_fail
    shuba_error $argv
    return 1
end

function shuba_version_at_least --argument-names shuba_actual_version shuba_minimum_version
    if not string match --regex --quiet '^(0|[1-9][0-9]*)([.](0|[1-9][0-9]*)){1,2}$' -- $shuba_actual_version
        return 2
    end
    if not string match --regex --quiet '^(0|[1-9][0-9]*)([.](0|[1-9][0-9]*)){1,2}$' -- $shuba_minimum_version
        return 2
    end

    set --local shuba_actual_parts (string split . -- $shuba_actual_version)
    set --local shuba_minimum_parts (string split . -- $shuba_minimum_version)
    while test (count $shuba_actual_parts) -lt 3
        set --append shuba_actual_parts 0
    end
    while test (count $shuba_minimum_parts) -lt 3
        set --append shuba_minimum_parts 0
    end

    for shuba_index in 1 2 3
        if test $shuba_actual_parts[$shuba_index] -gt $shuba_minimum_parts[$shuba_index]
            return 0
        end
        if test $shuba_actual_parts[$shuba_index] -lt $shuba_minimum_parts[$shuba_index]
            return 1
        end
    end
    return 0
end

function shuba_require_fish_version --argument-names shuba_minimum_version
    if not shuba_version_at_least $version $shuba_minimum_version
        shuba_fail "fish $shuba_minimum_version or newer is required; selected fish reports $version"
        return 1
    end
end

function shuba_resolve_command --argument-names shuba_command_name
    set --local shuba_command_path (command --search -- $shuba_command_name)
    if test $status -ne 0; or test (count $shuba_command_path) -ne 1
        shuba_fail "required command is unavailable: $shuba_command_name"
        return 1
    end

    set shuba_command_path (realpath --canonicalize-existing -- $shuba_command_path)
    if test $status -ne 0; or not test -f $shuba_command_path; or not test -x $shuba_command_path
        shuba_fail "required command does not resolve to an executable regular file: $shuba_command_name"
        return 1
    end
    printf '%s\n' $shuba_command_path
end

function shuba_require_regular_file --argument-names shuba_file_path
    if not test -f $shuba_file_path; or test -L $shuba_file_path; or not test -r $shuba_file_path
        shuba_fail "required readable non-symbolic-link regular file is unavailable: $shuba_file_path"
        return 1
    end
end

function shuba_require_executable_file --argument-names shuba_file_path
    shuba_require_regular_file $shuba_file_path; or return 1
    if not test -x $shuba_file_path
        shuba_fail "required file is not executable: $shuba_file_path"
        return 1
    end
end

function shuba_sha256_file --argument-names shuba_file_path
    shuba_require_regular_file $shuba_file_path; or return 1
    set --local shuba_hash_output (sha256sum <$shuba_file_path)
    if test $status -ne 0; or test (count $shuba_hash_output) -ne 1
        shuba_fail "could not hash file: $shuba_file_path"
        return 1
    end
    set --local shuba_digest (string split --max 1 ' ' -- $shuba_hash_output)[1]
    if not string match --regex --quiet '^[0-9a-f]{64}$' -- $shuba_digest
        shuba_fail "hash command returned an invalid SHA-256 for: $shuba_file_path"
        return 1
    end
    printf '%s\n' $shuba_digest
end

function shuba_first_line
    if test (count $argv) -eq 0
        shuba_fail 'first-line capture requires a command'
        return 1
    end
    set --local shuba_output (command $argv)
    set --local shuba_command_status $status
    if test $shuba_command_status -ne 0
        return $shuba_command_status
    end
    if test (count $shuba_output) -eq 0
        shuba_fail "command produced no output: $argv[1]"
        return 1
    end
    printf '%s\n' $shuba_output[1]
end

function shuba_validate_owned_directory --argument-names shuba_expected_path shuba_expected_parent shuba_label
    if test -L $shuba_expected_path
        shuba_fail "$shuba_label must not be a symbolic link: $shuba_expected_path"
        return 1
    end
    if not mkdir -p -- $shuba_expected_path
        shuba_fail "could not create $shuba_label: $shuba_expected_path"
        return 1
    end
    set --local shuba_physical_path (realpath --canonicalize-existing -- $shuba_expected_path)
    set --local shuba_physical_parent (realpath --canonicalize-existing -- $shuba_expected_parent)
    if test $status -ne 0; or test "$shuba_physical_path" != "$shuba_physical_parent/"(basename -- $shuba_expected_path)
        shuba_fail "$shuba_label resolved outside its expected parent: $shuba_expected_path"
        return 1
    end
end
