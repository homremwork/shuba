#!/usr/bin/fish --no-config

function shuba_android_workflow_test_fail
    printf 'Android release workflow tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_android_workflow_extract_step --argument-names shuba_name shuba_output
    set --local shuba_count (grep --fixed-strings --line-regexp --count -- "      - name: $shuba_name" $shuba_workflow_path)
    test "$shuba_count" = 1; or begin
        shuba_android_workflow_test_fail "expected exactly one workflow step: $shuba_name"
        return 1
    end
    awk -v expected="      - name: $shuba_name" '
        /^      - name: / {
            if (inside) exit
            inside = ($0 == expected)
        }
        inside && /^  [^ ]/ { exit }
        inside { print }
    ' $shuba_workflow_path >$shuba_output
end

function shuba_android_workflow_extract_script --argument-names shuba_step shuba_script
    awk '
        /^        run: [|]$/ { inside = 1; next }
        inside && /^          / { print substr($0, 11); next }
        inside { exit }
    ' $shuba_step >$shuba_script; or return 1
    test -s $shuba_script; or return 1
    fish --no-config --no-execute $shuba_script
end

function shuba_android_workflow_require_text --argument-names shuba_file shuba_text
    grep --fixed-strings --quiet -- $shuba_text $shuba_file; or begin
        shuba_android_workflow_test_fail "missing workflow requirement: $shuba_text"
        return 1
    end
end

function shuba_android_workflow_test_checkout
    set --local shuba_checkout $shuba_workflow_test_root/checkout
    set --local shuba_bundle $shuba_workflow_test_root/bundle
    shuba_android_workflow_extract_step 'Check out clean recursive candidate sources for publication' $shuba_checkout; or return 1
    shuba_android_workflow_extract_step 'Build and verify the complete public candidate bundle' $shuba_bundle; or return 1
    for shuba_text in \
        "if: inputs.operation == 'candidate'" \
        'ref: ${{ inputs.tag }}' \
        'path: build/publication-source' \
        'submodules: recursive' 'fetch-tags: true' 'persist-credentials: false'
        shuba_android_workflow_require_text $shuba_checkout $shuba_text; or return 1
    end
    grep --extended-regexp --quiet 'uses: actions/checkout@[0-9a-f]{40} ' $shuba_checkout; or return 1
    shuba_android_workflow_require_text $shuba_bundle 'working-directory: build/publication-source'; or return 1
    for shuba_argument in --packet-dir --source-dir --output --bundle-dir
        shuba_android_workflow_require_text $shuba_bundle "$shuba_argument "'$GITHUB_WORKSPACE/dist/'; or return 1
    end
    shuba_android_workflow_require_text $shuba_bundle 'test (git rev-parse HEAD) = "$SHUBA_TAG_COMMIT"; or exit 1'; or return 1
    shuba_android_workflow_require_text $shuba_bundle 'test (git rev-parse refs/tags/$SHUBA_TAG) = "$SHUBA_TAG_OBJECT"; or exit 1'; or return 1
    awk '
        /^      - name: Validate signed release evidence$/ { evidence = NR }
        /^      - name: Check out clean recursive candidate sources for publication$/ { checkout = NR }
        /^      - name: Build and verify the complete public candidate bundle$/ { bundle = NR }
        END { exit !(evidence > 0 && evidence < checkout && checkout < bundle) }
    ' $shuba_workflow_path; or begin
        shuba_android_workflow_test_fail 'publication checkout must follow signed evidence and precede bundle construction'
        return 1
    end
    shuba_android_workflow_extract_script $shuba_bundle $shuba_workflow_test_root/candidate.fish
end

function shuba_android_workflow_test_fish_gates
    # Actionlint validates YAML. This narrow reader checks the tracked literal
    # Fish run blocks, joining continuation lines before inspecting each gate.
    awk '
        /^      - name: / { name = $0; fish = 0; inside = 0 }
        /^        shell: fish [{]0[}]$/ { fish = 1; count += 1 }
        /^        run: [|]$/ { inside = fish; next }
        inside && /^          / {
            line = substr($0, 11)
            sub(/^[ ]+/, "", line)
            if (substr(line, length(line)) == sprintf("%c", 92)) {
                line = substr(line, 1, length(line) - 1)
                command = command line
                next
            }
            command = command line
            if (pending && command != "test $pipestatus[-1] -eq 0; or exit 1") {
                print name ": sdkmanager pipeline status is not checked" > "/dev/stderr"
                failed = 1
            }
            pending = (command == "yes | $sdkmanager --licenses >/dev/null")
            if (!pending && command !~ /; or exit 1$/) {
                print name ": missing explicit failure exit: " command > "/dev/stderr"
                failed = 1
            }
            command = ""
            next
        }
        { inside = 0 }
        END { exit (failed || pending || command != "" || count < 10) }
    ' $shuba_workflow_path; or return 1
end

function shuba_android_workflow_test_repository_selection
    awk '
        substr($0, length($0)) == sprintf("%c", 92) {
            command = command substr($0, 1, length($0) - 1)
            next
        }
        {
            command = command $0
            if (command ~ /^[ ]+gh (run download|release create) /) {
                count += 1
                if (index(command, "--repo \"$GITHUB_REPOSITORY\"") == 0) {
                    print "Missing explicit GitHub repository: " command > "/dev/stderr"
                    failed = 1
                }
            }
            command = ""
        }
        END { exit (failed || count != 4) }
    ' $shuba_workflow_path
end

function shuba_android_workflow_make_probes
    set --local shuba_library $shuba_workflow_test_root/probe/tools/release/lib
    mkdir -p -- $shuba_library $shuba_workflow_test_root/probe/bin; or return 1
    printf '%s\n' \
        'function probe --argument-names stage' \
        '    printf "%s\n" $stage >>$SHUBA_TEST_LOG' \
        '    test $stage != "$SHUBA_TEST_FAIL"' end >$shuba_library/core.fish; or return 1
    printf '%s\n' 'function shuba_contract_load' '    probe contract' end >$shuba_library/release-contract.fish; or return 1
    printf '%s\n' \
        'function shuba_exact_source_resolve_tag' \
        '    set -g shuba_exact_source_commit $SHUBA_TAG_COMMIT' \
        '    probe tag' end \
        'function shuba_exact_source_write_tag_notes' '    probe notes' end >$shuba_library/exact-source.fish; or return 1
    set --local shuba_files build-exact-source.fish build-publication-bundle.fish verify-publication-bundle.fish
    set --local shuba_stages source bundle verify
    for shuba_index in (seq 3)
        set --local shuba_executable $shuba_workflow_test_root/probe/tools/release/$shuba_files[$shuba_index]
        printf '#!%s --no-config\n' (status fish-path) >$shuba_executable; or return 1
        printf 'printf "%%s\\n" %s >>$SHUBA_TEST_LOG\ntest %s != "$SHUBA_TEST_FAIL"\n' \
            $shuba_stages[$shuba_index] $shuba_stages[$shuba_index] >>$shuba_executable; or return 1
        chmod 0755 -- $shuba_executable; or return 1
    end
    set --local shuba_git $shuba_workflow_test_root/probe/bin/git
    printf '#!%s --no-config\n' (status fish-path) >$shuba_git; or return 1
    printf '%s\n' \
        'if test "$argv[2]" = HEAD' \
        '    printf "%s\n" $SHUBA_TEST_HEAD_COMMIT' else \
        '    printf "%s\n" $SHUBA_TEST_TAG_OBJECT' end >>$shuba_git; or return 1
    chmod 0755 -- $shuba_git
end

function shuba_android_workflow_run_probe --argument-names shuba_failure shuba_expected_status shuba_expected_stages shuba_head shuba_object
    set --local shuba_probe $shuba_workflow_test_root/probe
    env SHUBA_TAG=v1.0.4 SHUBA_TAG_COMMIT=fixture-commit SHUBA_TAG_OBJECT=fixture-object \
        SHUBA_TEST_HEAD_COMMIT=$shuba_head SHUBA_TEST_TAG_OBJECT=$shuba_object \
        SHUBA_TEST_FAIL=$shuba_failure SHUBA_TEST_LOG=$shuba_probe/stages \
        RUNNER_TEMP=$shuba_probe GITHUB_WORKSPACE=$shuba_probe PATH=(string join : -- $shuba_probe/bin $PATH) \
        (status fish-path) --no-config $shuba_workflow_test_root/candidate.fish >$shuba_probe/output 2>&1
    set --local shuba_status $status
    set --local shuba_stages ''
    if test -f $shuba_probe/stages
        set shuba_stages (string join , -- (cat -- $shuba_probe/stages))
    end
    rm -f -- $shuba_probe/stages; or return 1
    if test $shuba_status -ne $shuba_expected_status; or test "$shuba_stages" != "$shuba_expected_stages"
        cat -- $shuba_probe/output >&2
        shuba_android_workflow_test_fail "failure=$shuba_failure head=$shuba_head object=$shuba_object status=$shuba_status stages=$shuba_stages"
        return 1
    end
end

function shuba_android_workflow_test_failure_propagation
    shuba_android_workflow_make_probes; or return 1
    pushd $shuba_workflow_test_root/probe >/dev/null; or return 1
    set --local shuba_stages contract tag notes source bundle verify
    for shuba_index in (seq (count $shuba_stages))
        shuba_android_workflow_run_probe $shuba_stages[$shuba_index] 1 \
            (string join , -- $shuba_stages[1..$shuba_index]) fixture-commit fixture-object; or return 1
    end
    shuba_android_workflow_run_probe '' 0 (string join , -- $shuba_stages) fixture-commit fixture-object; or return 1
    shuba_android_workflow_run_probe '' 1 contract,tag wrong-commit fixture-object; or return 1
    shuba_android_workflow_run_probe '' 1 contract,tag fixture-commit wrong-object; or return 1
    popd >/dev/null
end

function shuba_android_workflow_test_main
    set --local shuba_root (realpath --canonicalize-existing -- (status dirname)/../../..); or return 1
    set --global shuba_workflow_path $shuba_root/.github/workflows/android-release.yml
    set --global shuba_workflow_test_root (mktemp --directory /tmp/shuba-android-workflow-tests.XXXXXX); or return 1
    shuba_android_workflow_test_checkout; or return 1
    shuba_android_workflow_test_fish_gates; or return 1
    shuba_android_workflow_test_repository_selection; or return 1
    shuba_android_workflow_test_failure_propagation; or return 1
    printf '%s\n' 'Android release workflow checkout, repository selection, and failure-injection tests: passed'
end

set --global shuba_workflow_test_root ''
function shuba_android_workflow_test_cleanup --on-event fish_exit
    if test -n "$shuba_workflow_test_root"; and test -d $shuba_workflow_test_root
        rm -rf -- $shuba_workflow_test_root
    end
end

shuba_android_workflow_test_main
exit $status
