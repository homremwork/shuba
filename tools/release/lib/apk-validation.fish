function shuba_apk_capture --argument-names shuba_description shuba_output_path
    set --erase argv[1..2]
    set --local shuba_diagnostic $shuba_output_path.diagnostic
    command $argv >$shuba_output_path 2>$shuba_diagnostic
    set --local shuba_command_status $status
    if test $shuba_command_status -ne 0
        set --local shuba_detail (string join ' ' -- (string trim <$shuba_diagnostic))
        shuba_fail "$shuba_description failed: $shuba_detail"
        return 1
    end
end

function shuba_apk_require_fixed_line --argument-names shuba_path shuba_expected shuba_message
    set --local shuba_count (grep --fixed-strings --line-regexp --count -- $shuba_expected $shuba_path)
    if test $status -gt 1; or test "$shuba_count" != 1
        shuba_fail $shuba_message
        return 1
    end
end

function shuba_apk_xml_value --argument-names shuba_xml_path shuba_xpath
    set --local shuba_output (mktemp /tmp/shuba-apk-xml.XXXXXX); or return 1
    $shuba_xmlstarlet_path select --namespace android=http://schemas.android.com/apk/res/android \
        --template --output __SHUBA_APK_XML__ --value-of $shuba_xpath $shuba_xml_path >$shuba_output 2>/dev/null
    set --local shuba_xml_status $status
    set --local shuba_encoded (string collect <$shuba_output)
    rm -f -- $shuba_output
    if test $shuba_xml_status -ne 0; or not string match --quiet '__SHUBA_APK_XML__*' -- $shuba_encoded
        shuba_fail "could not query decoded APK manifest: $shuba_xpath"
        return 1
    end
    string replace __SHUBA_APK_XML__ '' -- $shuba_encoded | string collect
end

function shuba_apk_require_xml --argument-names shuba_xml_path shuba_xpath shuba_expected shuba_message
    set --local shuba_actual (shuba_apk_xml_value $shuba_xml_path $shuba_xpath | string collect); or return 1
    if test "$shuba_actual" != "$shuba_expected"
        shuba_fail $shuba_message
        return 1
    end
end

function shuba_apk_check_expectation --argument-names shuba_project_root shuba_apk_path shuba_artifact_kind
    if not contains -- $shuba_artifact_kind final upgrade-probe
        shuba_fail 'APK verification kind is unsupported'
        return 1
    end
    set --local shuba_source_code (shuba_contract_get app.version_code); or return 1
    if test $shuba_artifact_kind = final
        set --global shuba_apk_expected_basename (shuba_contract_get artifact.basename)
        set --global shuba_apk_expected_version_code $shuba_source_code
    else
        set --global shuba_apk_expected_version_code (math $shuba_source_code + 1)
        set --local shuba_probe_suffix "-upgrade-code-$shuba_apk_expected_version_code.apk"
        set --global shuba_apk_expected_basename (string replace --regex '[.]apk$' -- $shuba_probe_suffix (shuba_contract_get artifact.basename))
    end
    if test (path basename -- $shuba_apk_path) != $shuba_apk_expected_basename
        shuba_fail 'APK filename differs from the verification expectation'
        return 1
    end
    set --local shuba_physical_apk (realpath --canonicalize-existing -- $shuba_apk_path); or return 1
    if test $shuba_artifact_kind = upgrade-probe
        set --local shuba_non_final_root (realpath --canonicalize-existing -- $shuba_project_root/dist/non-final 2>/dev/null); or begin
            shuba_fail 'upgrade probe output root is unavailable'
            return 1
        end
        string match --quiet "$shuba_non_final_root/*" -- $shuba_physical_apk; or begin
            shuba_fail 'upgrade probe APK must reside below dist/non-final'
            return 1
        end
    else if string match --quiet "$shuba_project_root/dist/release/*" -- $shuba_physical_apk
        set --local shuba_upgrade_contamination (find -P $shuba_project_root/dist/release -maxdepth 1 -type f -name '*upgrade*' -print -quit)
        if test -n "$shuba_upgrade_contamination"
            shuba_fail 'final artifact directory contains an upgrade probe'
            return 1
        end
    end
end

function shuba_apk_check_signer_alignment --argument-names shuba_apk_path shuba_state_root
    shuba_apk_capture 'APK signature verification' $shuba_state_root/apksigner $shuba_apksigner_path verify --verbose --print-certs $shuba_apk_path; or return 1
    shuba_apk_require_fixed_line $shuba_state_root/apksigner 'Verified using v2 scheme (APK Signature Scheme v2): true' 'APK v2 signature verification failed'; or return 1
    shuba_apk_require_fixed_line $shuba_state_root/apksigner 'Number of signers: 1' 'APK must contain exactly one signer'; or return 1
    set --local shuba_fingerprints (sed -n 's/^Signer #[0-9][0-9]* certificate SHA-256 digest: \([0-9a-fA-F]\{64\}\)$/\1/p' $shuba_state_root/apksigner)
    if test (count $shuba_fingerprints) -ne 1; or test (string upper -- $shuba_fingerprints[1]) != (shuba_contract_get signing.certificate_sha256)
        shuba_fail 'APK signer certificate SHA-256 differs from the release contract'
        return 1
    end
    shuba_apk_capture 'APK alignment verification' $shuba_state_root/zipalign $shuba_zipalign_path -c -v 4 $shuba_apk_path; or return 1
    grep --extended-regexp --quiet 'Verification success?ful' $shuba_state_root/zipalign; or begin
        shuba_fail 'zipalign did not confirm APK alignment'
        return 1
    end
end

function shuba_apk_check_manifest --argument-names shuba_apk_path shuba_state_root
    set --local shuba_manifest $shuba_state_root/AndroidManifest.xml
    shuba_apk_capture 'APK manifest decoding' $shuba_manifest $shuba_apkanalyzer_path manifest print $shuba_apk_path; or return 1
    $shuba_xmlstarlet_path validate --well-formed --quiet $shuba_manifest >/dev/null 2>&1; or begin
        shuba_fail 'decoded APK manifest is not well-formed XML'
        return 1
    end
    for shuba_record in \
        'string(/manifest/@package)|app.application_id' \
        'string(/manifest/@android:versionCode)|__version_code__' \
        'string(/manifest/@android:versionName)|app.version_name' \
        'string(/manifest/uses-sdk/@android:minSdkVersion)|android.min_sdk' \
        'string(/manifest/uses-sdk/@android:targetSdkVersion)|android.target_sdk'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        set --local shuba_expected $shuba_fields[2]
        if test $shuba_expected = __version_code__
            set shuba_expected $shuba_apk_expected_version_code
        else
            set shuba_expected (shuba_contract_get $shuba_expected); or return 1
        end
        shuba_apk_require_xml $shuba_manifest $shuba_fields[1] $shuba_expected "APK identity differs from the release contract: $shuba_fields[1]"; or return 1
    end
    for shuba_permission in (string split , -- (shuba_contract_get android.forbidden_permissions))
        set --local shuba_permission_xpath (string join '' -- "count(/manifest/uses-permission[@android:name='" $shuba_permission "'])")
        set --local shuba_permission_count (shuba_apk_xml_value $shuba_manifest $shuba_permission_xpath); or return 1
        if test $shuba_permission_count -ne 0
            shuba_fail 'APK manifest requests a forbidden release-contract permission'
            return 1
        end
    end
    for shuba_record in \
        'debuggable|APK is debuggable' \
        'testOnly|APK is marked test-only'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        set --local shuba_attribute_count (shuba_apk_xml_value $shuba_manifest "count(/manifest/application/@android:$shuba_fields[1])"); or return 1
        if test $shuba_attribute_count -gt 1
            shuba_fail "APK manifest repeats application attribute: $shuba_fields[1]"
            return 1
        end
        if test $shuba_attribute_count -eq 1
            shuba_apk_require_xml $shuba_manifest "string(/manifest/application/@android:$shuba_fields[1])" false $shuba_fields[2]; or return 1
        end
    end
    for shuba_record in \
        'count(/manifest/application)|1' \
        'string(/manifest/application/@android:label)|@ref/0x7f020000' \
        'count(/manifest/application/activity)|1' \
        'string(/manifest/application/activity/@android:name)|android.app.Activity' \
        'string(/manifest/application/activity/@android:exported)|true' \
        'count(/manifest/application/activity/intent-filter)|1' \
        'count(/manifest/application/activity/intent-filter/action)|1' \
        'string(/manifest/application/activity/intent-filter/action/@android:name)|android.intent.action.MAIN' \
        'count(/manifest/application/activity/intent-filter/category)|1' \
        'string(/manifest/application/activity/intent-filter/category/@android:name)|android.intent.category.LAUNCHER' \
        'count(/manifest/application/receiver)|1' \
        'string(/manifest/application/receiver/@android:name)|com.rmsl.juce.Receiver' \
        'string(/manifest/application/receiver/@android:exported)|false' \
        'count(/manifest/application/activity-alias) + count(/manifest/application/service) + count(/manifest/application/provider) + count(/manifest/instrumentation)|0'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_apk_require_xml $shuba_manifest $shuba_fields[1] "$shuba_fields[2]" "APK manifest component contract failed: $shuba_fields[1]"; or return 1
    end
    set --local shuba_badging $shuba_state_root/badging
    shuba_apk_capture 'APK badging inspection' $shuba_badging $shuba_aapt2_path dump badging $shuba_apk_path; or return 1
    set --local shuba_label_line (grep --fixed-strings --line-regexp -- "application-label:'"(shuba_contract_get app.name)"'" $shuba_badging)
    if test $status -ne 0; or test (count $shuba_label_line) -ne 1
        shuba_fail 'APK application label differs from the release contract'
        return 1
    end
end

function shuba_apk_check_archive --argument-names shuba_apk_path shuba_state_root
    set --local shuba_extract_root $shuba_state_root/extracted
    mkdir --mode 0700 -- $shuba_extract_root; or return 1
    set --local shuba_archive_diagnostic $shuba_state_root/bsdtar.diagnostic
    $shuba_bsdtar_path --extract --file $shuba_apk_path --directory $shuba_extract_root --no-same-owner --no-same-permissions 2>$shuba_archive_diagnostic
    if test $status -ne 0; or test -s $shuba_archive_diagnostic
        shuba_fail 'APK extraction failed or produced a warning'
        return 1
    end
    set --local shuba_invalid_entry (find -P $shuba_extract_root \( -type l -o -type b -o -type c -o -type p -o -type s \) -print -quit)
    if test -n "$shuba_invalid_entry"
        shuba_fail 'APK extraction produced a link, device, pipe, or socket'
        return 1
    end
    set --local shuba_native_paths $shuba_state_root/native-paths
    find -P $shuba_extract_root/lib -type f -print0 | sort --zero-terminated >$shuba_native_paths
    set --local shuba_find_statuses $pipestatus
    for shuba_status in $shuba_find_statuses
        if test $shuba_status -ne 0
            shuba_fail 'could not enumerate extracted APK native libraries'
            return 1
        end
    end
    set --local shuba_native_entries
    while read --null shuba_native_path
        set --append shuba_native_entries (string replace "$shuba_extract_root/" '' -- $shuba_native_path | string collect)
    end <$shuba_native_paths
    if test (count $shuba_native_entries) -ne 1; or test $shuba_native_entries[1] != (shuba_contract_get artifact.native_library_path)
        shuba_fail 'APK native payload differs from the release contract'
        return 1
    end
    for shuba_required in AndroidManifest.xml resources.arsc
        shuba_require_regular_file $shuba_extract_root/$shuba_required; or return 1
    end
    set --global shuba_apk_extracted_library $shuba_extract_root/(shuba_contract_get artifact.native_library_path)
end

function shuba_apk_check_elf_icons --argument-names shuba_apk_path shuba_state_root
    shuba_apk_capture 'APK native ELF header inspection' $shuba_state_root/elf-header $shuba_ndk_readelf_path --file-header $shuba_apk_extracted_library; or return 1
    grep --extended-regexp --quiet '^[[:space:]]*Machine:[[:space:]]+AArch64[[:space:]]*$' $shuba_state_root/elf-header; or begin
        shuba_fail 'APK native library is not AArch64'
        return 1
    end
    shuba_apk_capture 'APK native ELF section inspection' $shuba_state_root/elf-sections $shuba_ndk_readelf_path --section-headers --wide $shuba_apk_extracted_library; or return 1
    if grep --extended-regexp --quiet '] [[:space:]]*[.](debug|zdebug)|] [[:space:]]*[.](symtab|strtab)([[:space:]]|$)' $shuba_state_root/elf-sections
        shuba_fail 'APK native library contains unstripped sections'
        return 1
    end
    set --local shuba_configurations ($shuba_apkanalyzer_path resources configs --type drawable --package (shuba_contract_get app.application_id) $shuba_apk_path 2>/dev/null); or return 1
    for shuba_configuration in ldpi mdpi hdpi xhdpi anydpi
        contains -- $shuba_configuration $shuba_configurations; or begin
            shuba_fail "APK launcher icon lacks the $shuba_configuration resource"
            return 1
        end
    end
    set --local shuba_resources (mktemp /tmp/shuba-apk-resources.XXXXXX); or return 1
    shuba_apk_capture 'APK resource inspection' $shuba_resources $shuba_aapt2_path dump resources $shuba_apk_path; or begin
        rm -f -- $shuba_resources $shuba_resources.diagnostic
        return 1
    end
    grep --extended-regexp --quiet '^[[:space:]]*resource 0x[0-9a-f]+ drawable/icon$' $shuba_resources; or begin
        rm -f -- $shuba_resources $shuba_resources.diagnostic
        shuba_fail 'APK lacks drawable/icon'
        return 1
    end
    rm -f -- $shuba_resources $shuba_resources.diagnostic
end

function shuba_validate_android_apk --argument-names shuba_project_root shuba_apk_path shuba_artifact_kind
    shuba_require_regular_file $shuba_apk_path; or return 1
    shuba_apk_check_expectation $shuba_project_root $shuba_apk_path $shuba_artifact_kind; or return 1
    set --local shuba_state_root (mktemp --directory /tmp/shuba-apk-validation.XXXXXX); or return 1
    shuba_apk_check_signer_alignment $shuba_apk_path $shuba_state_root
    and shuba_apk_check_manifest $shuba_apk_path $shuba_state_root
    and shuba_apk_check_archive $shuba_apk_path $shuba_state_root
    and shuba_apk_check_elf_icons $shuba_apk_path $shuba_state_root
    set --local shuba_validation_status $status
    rm -rf -- $shuba_state_root
    set --erase --global shuba_apk_extracted_library
    if test $shuba_validation_status -ne 0
        return $shuba_validation_status
    end
    printf 'Android APK verification: %s %s code %s (%s) is signed, aligned, contract-consistent, and stripped.\n' \
        (shuba_contract_get app.name) (shuba_contract_get app.version_name) $shuba_apk_expected_version_code (shuba_contract_get android.abi)
end
