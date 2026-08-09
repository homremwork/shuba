#!/usr/bin/fish --no-config

function shuba_signing_usage
    printf '%s\n' \
        'Usage: tools/release/prepare-android-release-signing.fish [OPTION] [-- COMMAND [ARG...]]' \
        '' \
        'Validate external Android release-signing variables and optionally run' \
        'COMMAND with environment-backed Gradle project properties.' \
        '' \
        'Options:' \
        '  --check       validate inputs, PKCS12 alias, and pinned certificate only' \
        '  --print-init  print the tracked Gradle init-script path after validation' \
        '  --help        show this help'
end

function shuba_signing_require_environment --argument-names shuba_variable_name
    if not set --query $shuba_variable_name
        shuba_fail "required environment variable is absent or empty: $shuba_variable_name"
        return 1
    end
    set --local shuba_variable_value $$shuba_variable_name
    if test -z "$shuba_variable_value"
        shuba_fail "required environment variable is absent or empty: $shuba_variable_name"
        return 1
    end
    if string match --regex --quiet '[\n\r]' -- $shuba_variable_value
        shuba_fail "environment variable contains a forbidden line break: $shuba_variable_name"
        return 1
    end
end

function shuba_signing_validate_store
    set --local shuba_listing (mktemp /tmp/shuba-signing-listing.XXXXXX); or return 1
    begin
        set --local --export SHUBA_ANDROID_STORE_PASSWORD "$SHUBA_ANDROID_STORE_PASSWORD"
        $shuba_keytool_path -list -keystore $SHUBA_ANDROID_KEYSTORE_FILE -storetype PKCS12 \
            -storepass:env SHUBA_ANDROID_STORE_PASSWORD -alias $SHUBA_ANDROID_KEY_ALIAS >$shuba_listing 2>/dev/null
    end
    set --local shuba_store_status $status
    if test $shuba_store_status -ne 0
        rm -f -- $shuba_listing
        shuba_fail 'keystore alias/password validation failed'
        return 1
    end
    grep --fixed-strings --quiet -- "$SHUBA_ANDROID_KEY_ALIAS," $shuba_listing
    and grep --fixed-strings --quiet -- PrivateKeyEntry $shuba_listing
    set --local shuba_entry_status $status
    rm -f -- $shuba_listing
    if test $shuba_entry_status -ne 0
        shuba_fail 'keystore alias is not a private signing entry'
        return 1
    end
end

function shuba_signing_validate_private_key
    set --local shuba_temporary_directory (mktemp --directory /tmp/shuba-android-signing.XXXXXX); or return 1
    set --local shuba_request $shuba_temporary_directory/signing-request.pem
    begin
        set --local --export SHUBA_ANDROID_STORE_PASSWORD "$SHUBA_ANDROID_STORE_PASSWORD"
        set --local --export SHUBA_ANDROID_KEY_PASSWORD "$SHUBA_ANDROID_KEY_PASSWORD"
        $shuba_keytool_path -certreq -keystore $SHUBA_ANDROID_KEYSTORE_FILE -storetype PKCS12 \
            -storepass:env SHUBA_ANDROID_STORE_PASSWORD -keypass:env SHUBA_ANDROID_KEY_PASSWORD \
            -alias $SHUBA_ANDROID_KEY_ALIAS -rfc -file $shuba_request >/dev/null 2>&1
    end
    set --local shuba_request_status $status
    if test $shuba_request_status -ne 0; or not test -s $shuba_request
        rm -rf -- $shuba_temporary_directory
        shuba_fail 'keystore private-key password validation failed'
        return 1
    end
    rm -rf -- $shuba_temporary_directory
end

function shuba_signing_certificate_fingerprint
    set --local shuba_certificate (mktemp /tmp/shuba-signing-certificate.XXXXXX); or return 1
    set --local shuba_fingerprint_output (mktemp /tmp/shuba-signing-fingerprint.XXXXXX); or begin
        rm -f -- $shuba_certificate
        return 1
    end
    begin
        set --local --export SHUBA_ANDROID_STORE_PASSWORD "$SHUBA_ANDROID_STORE_PASSWORD"
        $shuba_keytool_path -exportcert -rfc -keystore $SHUBA_ANDROID_KEYSTORE_FILE -storetype PKCS12 \
            -storepass:env SHUBA_ANDROID_STORE_PASSWORD -alias $SHUBA_ANDROID_KEY_ALIAS >$shuba_certificate 2>/dev/null
    end
    and $shuba_openssl_path x509 -in $shuba_certificate -noout -fingerprint -sha256 >$shuba_fingerprint_output 2>/dev/null
    set --local shuba_fingerprint_status $status
    if test $shuba_fingerprint_status -ne 0
        rm -f -- $shuba_certificate $shuba_fingerprint_output
        shuba_fail 'could not calculate the signing certificate SHA-256'
        return 1
    end
    set --local shuba_fingerprint (string replace --regex '^.*=' '' < $shuba_fingerprint_output | string replace --all : '' | string trim)
    rm -f -- $shuba_certificate $shuba_fingerprint_output
    if not string match --regex --quiet '^[0-9A-F]{64}$' -- $shuba_fingerprint
        shuba_fail 'keytool returned an invalid signing certificate SHA-256'
        return 1
    end
    printf '%s\n' $shuba_fingerprint
end

function shuba_signing_validate_external_inputs
    for shuba_variable_name in \
        SHUBA_ANDROID_KEYSTORE_FILE SHUBA_ANDROID_KEY_ALIAS \
        SHUBA_ANDROID_STORE_PASSWORD SHUBA_ANDROID_KEY_PASSWORD
        shuba_signing_require_environment $shuba_variable_name; or return 1
    end
    shuba_require_regular_file $SHUBA_ANDROID_KEYSTORE_FILE; or begin
        shuba_fail 'SHUBA_ANDROID_KEYSTORE_FILE must identify a readable non-symbolic-link regular file'
        return 1
    end
    if not string match --regex --quiet '^[A-Za-z0-9._-]+$' -- $SHUBA_ANDROID_KEY_ALIAS
        shuba_fail 'SHUBA_ANDROID_KEY_ALIAS contains unsupported characters'
        return 1
    end
    shuba_signing_validate_store; or return 1
    shuba_signing_validate_private_key; or return 1
    set --local shuba_fingerprint (shuba_signing_certificate_fingerprint); or return 1
    if test $shuba_fingerprint != (shuba_contract_get signing.certificate_sha256)
        shuba_fail 'keystore certificate SHA-256 does not match the public release contract'
        return 1
    end
end

function shuba_signing_main
    set --local shuba_mode run
    if test (count $argv) -gt 0
        switch $argv[1]
            case --help
                shuba_signing_usage
                return 0
            case --check
                set shuba_mode check
                set --erase argv[1]
            case --print-init
                set shuba_mode print-init
                set --erase argv[1]
            case --
                set --erase argv[1]
            case '--*'
                shuba_signing_usage >&2
                shuba_fail "unknown option: $argv[1]"
                return 1
        end
    end
    if test $shuba_mode = run
        if test (count $argv) -eq 0
            shuba_signing_usage >&2
            shuba_fail 'a command must follow --'
            return 1
        end
    else if test (count $argv) -ne 0
        shuba_fail "$shuba_mode accepts no additional arguments"
        return 1
    end

    set --local shuba_project_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    set --local shuba_init_script (status dirname)/android-release-signing.init.gradle
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_require_regular_file $shuba_init_script; or return 1
    set --global shuba_keytool_path (shuba_resolve_command keytool); or return 1
    set --global shuba_openssl_path (shuba_resolve_command openssl); or return 1
    shuba_signing_validate_external_inputs; or return 1

    switch $shuba_mode
        case check
            printf '%s\n' 'Android release signing: PKCS12 private-key alias and pinned certificate are valid.'
        case print-init
            realpath --canonicalize-existing -- $shuba_init_script
        case run
            begin
                set --local --export ORG_GRADLE_PROJECT_shubaReleaseKeyStoreFile "$SHUBA_ANDROID_KEYSTORE_FILE"
                set --local --export ORG_GRADLE_PROJECT_shubaReleaseKeyAlias "$SHUBA_ANDROID_KEY_ALIAS"
                set --local --export ORG_GRADLE_PROJECT_shubaReleaseStorePassword "$SHUBA_ANDROID_STORE_PASSWORD"
                set --local --export ORG_GRADLE_PROJECT_shubaReleaseKeyPassword "$SHUBA_ANDROID_KEY_PASSWORD"
                set --erase SHUBA_ANDROID_KEYSTORE_FILE SHUBA_ANDROID_KEY_ALIAS \
                    SHUBA_ANDROID_STORE_PASSWORD SHUBA_ANDROID_KEY_PASSWORD
                $argv
            end
    end
end

umask 077
set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish

shuba_signing_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
