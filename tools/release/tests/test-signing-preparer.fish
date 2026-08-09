#!/usr/bin/fish --no-config

function shuba_signing_test_fail
    printf 'R12F signing preparer tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_signing_test_run
    set --global shuba_signing_test_output (env PATH=$shuba_signing_test_path $argv 2>&1)
    set --global shuba_signing_test_status $status
end

function shuba_signing_test_require_rejection --argument-names shuba_description
    set --erase argv[1]
    shuba_signing_test_run $argv
    if test $shuba_signing_test_status -eq 0
        shuba_signing_test_fail "accepted $shuba_description"
        return 1
    end
    for shuba_secret in $SHUBA_ANDROID_STORE_PASSWORD $SHUBA_ANDROID_KEY_PASSWORD
        if string match --quiet "*$shuba_secret*" -- (string join \n -- $shuba_signing_test_output)
            shuba_signing_test_fail "leaked a password while rejecting $shuba_description"
            return 1
        end
    end
end

function shuba_signing_test_write_executable --argument-names shuba_path shuba_body
    printf '%s\n' '#!/bin/sh' $shuba_body >$shuba_path; or return 1
    chmod 0700 $shuba_path
end

function shuba_signing_test_main
    set --local shuba_script_directory (status dirname)
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    set --local shuba_preparer $shuba_project_root/tools/release/prepare-android-release-signing.fish
    set --global shuba_signing_test_root (mktemp --directory /tmp/shuba-r12f-signing-tests.XXXXXX); or return 1
    set --global shuba_signing_test_path $shuba_signing_test_root/bin:/usr/bin
    mkdir $shuba_signing_test_root/bin; or return 1
    ln -s /usr/bin/env $shuba_signing_test_root/bin/env
    ln -s /usr/bin/mktemp $shuba_signing_test_root/bin/mktemp
    ln -s /usr/bin/rm $shuba_signing_test_root/bin/rm
    ln -s /usr/bin/grep $shuba_signing_test_root/bin/grep
    ln -s /usr/bin/realpath $shuba_signing_test_root/bin/realpath
    ln -s /usr/bin/fish $shuba_signing_test_root/bin/fish

    set --global --export SHUBA_ANDROID_KEYSTORE_FILE $shuba_signing_test_root/release.p12
    set --global --export SHUBA_ANDROID_KEY_ALIAS shuba-release
    set --global --export SHUBA_ANDROID_STORE_PASSWORD store-secret-value
    set --global --export SHUBA_ANDROID_KEY_PASSWORD key-secret-value
    printf 'fixture\n' >$SHUBA_ANDROID_KEYSTORE_FILE
    chmod 0600 $SHUBA_ANDROID_KEYSTORE_FILE

    shuba_signing_test_write_executable $shuba_signing_test_root/bin/keytool \
        'command_line=$(tr "\000" "\n" <"/proc/$$/cmdline") || exit 1
case "$command_line" in
    *store-secret-value*|*key-secret-value*) exit 97 ;;
esac
case " $* " in
    *" -list "*)
        [ "$SHUBA_SIGNING_TEST_MODE" != wrong-store ] || exit 1
        printf "%s\n" "shuba-release, PrivateKeyEntry"
        ;;
    *" -certreq "*)
        [ "$SHUBA_SIGNING_TEST_MODE" != wrong-key ] || exit 1
        while [ "$#" -gt 0 ]; do [ "$1" = -file ] && { shift; printf "%s\n" request >"$1"; exit 0; }; shift; done
        exit 1
        ;;
    *" -exportcert "*)
        printf "%s\n" certificate
        ;;
    *) exit 1 ;;
esac'; or return 1
    shuba_signing_test_write_executable $shuba_signing_test_root/bin/openssl \
        'if [ "$SHUBA_SIGNING_TEST_MODE" = wrong-certificate ]; then digest=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA; else digest=1BA3E5BF7A0A59407407C2CA10F6E8A42654F5303FE218FC610EB4EAC7FDE861; fi
printf "sha256 Fingerprint=%s\n" "$digest"'; or return 1

    set --global --export SHUBA_SIGNING_TEST_MODE valid
    shuba_signing_test_run $shuba_preparer --check
    if test $shuba_signing_test_status -ne 0; or test (string join \n -- $shuba_signing_test_output) != 'Android release signing: PKCS12 private-key alias and pinned certificate are valid.'
        shuba_signing_test_fail 'positive --check failed'
        return 1
    end

    set --local shuba_probe $shuba_signing_test_root/probe
    shuba_signing_test_write_executable $shuba_probe \
        'command_line=$(tr "\000" "\n" <"/proc/$$/cmdline") || exit 1
case "$command_line" in
    *store-secret-value*|*key-secret-value*) exit 97 ;;
esac
printf "%s\0" "$@" >"$SHUBA_SIGNING_TEST_ARGUMENTS"
printf "%s\n" "$ORG_GRADLE_PROJECT_shubaReleaseKeyStoreFile" "$ORG_GRADLE_PROJECT_shubaReleaseKeyAlias" "$ORG_GRADLE_PROJECT_shubaReleaseStorePassword" "$ORG_GRADLE_PROJECT_shubaReleaseKeyPassword" >"$SHUBA_SIGNING_TEST_ENVIRONMENT"
env | grep -E "^SHUBA_ANDROID_(KEYSTORE_FILE|KEY_ALIAS|STORE_PASSWORD|KEY_PASSWORD)=" >/dev/null && exit 98
exit 0'; or return 1
    set --global --export SHUBA_SIGNING_TEST_ARGUMENTS $shuba_signing_test_root/arguments
    set --global --export SHUBA_SIGNING_TEST_ENVIRONMENT $shuba_signing_test_root/environment
    shuba_signing_test_run $shuba_preparer -- $shuba_probe 'argument one' '*literal*' ''
    if test $shuba_signing_test_status -ne 0
        printf 'R12F signing preparer tests: positive child execution failed with status %s: %s\n' \
            $shuba_signing_test_status (string join ' | ' -- $shuba_signing_test_output) >&2
        return 1
    end
    set --local shuba_arguments
    while read --null shuba_argument
        set --append shuba_arguments $shuba_argument
    end <$SHUBA_SIGNING_TEST_ARGUMENTS
    if test (count $shuba_arguments) -ne 3; or test "$shuba_arguments[1]" != 'argument one'; or test "$shuba_arguments[2]" != '*literal*'; or test -n "$shuba_arguments[3]"
        shuba_signing_test_fail 'child arguments were not preserved exactly'
        return 1
    end
    set --local shuba_environment (string split \n -- (string collect <$SHUBA_SIGNING_TEST_ENVIRONMENT))
    if test "$shuba_environment[1]" != "$SHUBA_ANDROID_KEYSTORE_FILE"; or test "$shuba_environment[2]" != "$SHUBA_ANDROID_KEY_ALIAS"; or test "$shuba_environment[3]" != "$SHUBA_ANDROID_STORE_PASSWORD"; or test "$shuba_environment[4]" != "$SHUBA_ANDROID_KEY_PASSWORD"
        shuba_signing_test_fail 'Gradle child environment properties are incomplete'
        return 1
    end
    if grep --extended-regexp --quiet 'env[[:space:]]+.*(STORE|KEY).*PASSWORD=' $shuba_preparer
        shuba_signing_test_fail 'signing preparer places a password assignment in an env command argument'
        return 1
    end

    shuba_signing_test_require_rejection absent-variable env -u SHUBA_ANDROID_KEY_ALIAS $shuba_preparer --check; or return 1
    shuba_signing_test_require_rejection empty-variable env SHUBA_ANDROID_KEY_ALIAS= $shuba_preparer --check; or return 1
    shuba_signing_test_require_rejection line-broken-variable env "SHUBA_ANDROID_KEY_ALIAS=bad\nalias" $shuba_preparer --check; or return 1
    shuba_signing_test_require_rejection malformed-alias env SHUBA_ANDROID_KEY_ALIAS='bad/alias' $shuba_preparer --check; or return 1
    ln -s $SHUBA_ANDROID_KEYSTORE_FILE $shuba_signing_test_root/release-link.p12
    shuba_signing_test_require_rejection symbolic-link-keystore env SHUBA_ANDROID_KEYSTORE_FILE=$shuba_signing_test_root/release-link.p12 $shuba_preparer --check; or return 1
    set --global --export SHUBA_SIGNING_TEST_MODE wrong-store
    shuba_signing_test_require_rejection wrong-store-password $shuba_preparer --check; or return 1
    set --global --export SHUBA_SIGNING_TEST_MODE wrong-key
    shuba_signing_test_require_rejection wrong-key-password $shuba_preparer --check; or return 1
    set --global --export SHUBA_SIGNING_TEST_MODE wrong-certificate
    shuba_signing_test_require_rejection wrong-certificate $shuba_preparer --check; or return 1

    set --global --export SHUBA_SIGNING_TEST_MODE valid
    set --local shuba_init_output (env PATH=$shuba_signing_test_path $shuba_preparer --print-init); or return 1
    test "$shuba_init_output" = $shuba_project_root/tools/release/android-release-signing.init.gradle; or return 1
    printf '%s\n' 'R12F Android signing preparer input, secret-boundary, and child-argument probes: passed'
end

set --global shuba_signing_test_root ''
function shuba_signing_test_cleanup --on-event fish_exit
    if test -n "$shuba_signing_test_root"; and test -d $shuba_signing_test_root
        rm -rf -- $shuba_signing_test_root
    end
end

shuba_signing_test_main
set --local shuba_main_status $status
exit $shuba_main_status
