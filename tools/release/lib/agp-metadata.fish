function shuba_find_android_release_apk --argument-names shuba_output_root shuba_application_id shuba_version_code shuba_version_name
    if not test -d $shuba_output_root; or test -L $shuba_output_root
        shuba_fail 'APK output root must be a non-symbolic-link directory'
        return 1
    end
    if not string match --regex --quiet '^[a-z][a-z0-9_]*([.][a-z][a-z0-9_]*)+$' -- $shuba_application_id
        shuba_fail 'APK discovery application ID is malformed'
        return 1
    end
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_version_code
        shuba_fail 'APK discovery version code must be a positive decimal integer'
        return 1
    end
    if not string match --regex --quiet '^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)$' -- $shuba_version_name
        shuba_fail 'APK discovery version name is malformed'
        return 1
    end
    if not set --query shuba_jq_path
        shuba_fail 'APK discovery requires the validated jq tool selection'
        return 1
    end
    if not set --query shuba_release_tools_root
        shuba_fail 'APK discovery requires the release-tool root'
        return 1
    end

    set --local shuba_query_path $shuba_release_tools_root/queries/agp-output-metadata.jq
    shuba_require_regular_file $shuba_query_path; or return 1
    set --local shuba_physical_output_root (realpath --canonicalize-existing -- $shuba_output_root)
    if test $status -ne 0
        shuba_fail 'could not resolve the APK output root'
        return 1
    end

    set --local shuba_temporary_directory (mktemp --directory /tmp/shuba-agp-metadata.XXXXXX)
    if test $status -ne 0
        shuba_fail 'could not allocate APK discovery workspace'
        return 1
    end
    set --local shuba_metadata_paths $shuba_temporary_directory/metadata.paths
    find -P $shuba_physical_output_root \( -type f -o -type l \) -name output-metadata.json -print0 \
        | sort --zero-terminated >$shuba_metadata_paths
    set --local shuba_find_statuses $pipestatus
    for shuba_find_status in $shuba_find_statuses
        if test $shuba_find_status -ne 0
            rm -rf -- $shuba_temporary_directory
            shuba_fail 'could not enumerate generated APK output metadata'
            return 1
        end
    end
    if not test -s $shuba_metadata_paths
        rm -rf -- $shuba_temporary_directory
        shuba_fail 'generated APK output contains no output-metadata.json files'
        return 1
    end

    set --local shuba_release_apks
    while read --null shuba_metadata_path
        if string match --regex --quiet '[[:cntrl:]]' -- $shuba_metadata_path
            rm -rf -- $shuba_temporary_directory
            shuba_fail 'generated APK output metadata path contains a control character'
            return 1
        end
        if not test -f $shuba_metadata_path; or test -L $shuba_metadata_path; or not test -r $shuba_metadata_path
            rm -rf -- $shuba_temporary_directory
            shuba_fail "output metadata must be a readable non-symbolic-link regular file: $shuba_metadata_path"
            return 1
        end

        set --local shuba_jq_diagnostic $shuba_temporary_directory/jq.diagnostic
        set --local shuba_metadata_result ($shuba_jq_path \
            --exit-status \
            --raw-output \
            --arg application_id $shuba_application_id \
            --argjson version_code $shuba_version_code \
            --arg version_name $shuba_version_name \
            --from-file $shuba_query_path \
            $shuba_metadata_path 2>$shuba_jq_diagnostic)
        set --local shuba_jq_status $status
        if test $shuba_jq_status -ne 0; or test (count $shuba_metadata_result) -ne 1
            set --local shuba_detail (string join ' ' -- (string trim < $shuba_jq_diagnostic))
            rm -rf -- $shuba_temporary_directory
            shuba_fail "could not validate generated output metadata $shuba_metadata_path: $shuba_detail"
            return 1
        end
        if test $shuba_metadata_result = ignore
            continue
        end

        set --local shuba_result_fields (string split \t -- $shuba_metadata_result)
        if test (count $shuba_result_fields) -ne 2; or test $shuba_result_fields[1] != release
            rm -rf -- $shuba_temporary_directory
            shuba_fail 'jq returned an invalid bounded APK metadata record'
            return 1
        end
        set --local shuba_apk_path (path normalize -- $shuba_metadata_path/../$shuba_result_fields[2])
        if not test -f $shuba_apk_path; or test -L $shuba_apk_path; or not test -r $shuba_apk_path
            rm -rf -- $shuba_temporary_directory
            shuba_fail "generated Release APK is not a readable non-symbolic-link regular file: $shuba_apk_path"
            return 1
        end
        set shuba_apk_path (realpath --canonicalize-existing -- $shuba_apk_path)
        set --local shuba_relative_apk (realpath --relative-to $shuba_physical_output_root -- $shuba_apk_path)
        if test $status -ne 0; or test $shuba_relative_apk = ..; or string match --quiet '../*' -- $shuba_relative_apk
            rm -rf -- $shuba_temporary_directory
            shuba_fail 'generated Release APK resolves outside the APK output root'
            return 1
        end
        set --append shuba_release_apks $shuba_apk_path
    end <$shuba_metadata_paths

    rm -rf -- $shuba_temporary_directory
    if test (count $shuba_release_apks) -ne 1
        shuba_fail "expected exactly one release_Release APK metadata entry, found "(count $shuba_release_apks)
        return 1
    end
    printf '%s\n' $shuba_release_apks[1]
end
