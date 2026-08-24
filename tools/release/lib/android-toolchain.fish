function shuba_resolve_android_sdk_root
    set --local shuba_candidates
    if set --query ANDROID_SDK_ROOT; and test -n "$ANDROID_SDK_ROOT"
        set --append shuba_candidates $ANDROID_SDK_ROOT
    end
    if set --query ANDROID_HOME; and test -n "$ANDROID_HOME"
        set --append shuba_candidates $ANDROID_HOME
    end
    set --append shuba_candidates /opt/android-sdk

    set --local shuba_resolved_candidates
    for shuba_candidate in $shuba_candidates
        set --local shuba_resolved (realpath --canonicalize-existing -- $shuba_candidate 2>/dev/null)
        if test $status -eq 0; and test -d $shuba_resolved; and test -x $shuba_resolved/platform-tools/adb
            if not contains -- $shuba_resolved $shuba_resolved_candidates
                set --append shuba_resolved_candidates $shuba_resolved
            end
        end
    end
    if test (count $shuba_resolved_candidates) -eq 0
        shuba_fail 'ANDROID_SDK_ROOT or ANDROID_HOME must identify an SDK containing platform-tools/adb'
        return 1
    end
    if set --query ANDROID_SDK_ROOT; and test -n "$ANDROID_SDK_ROOT"; and set --query ANDROID_HOME; and test -n "$ANDROID_HOME"
        set --local shuba_sdk_root (realpath --canonicalize-existing -- $ANDROID_SDK_ROOT 2>/dev/null)
        set --local shuba_android_home (realpath --canonicalize-existing -- $ANDROID_HOME 2>/dev/null)
        if test "$shuba_sdk_root" != "$shuba_android_home"
            shuba_fail 'ANDROID_SDK_ROOT and ANDROID_HOME resolve to different directories'
            return 1
        end
    end
    printf '%s\n' $shuba_resolved_candidates[1]
end

function shuba_extract_first_semantic_version
    for shuba_line in $argv
        set --local shuba_match (string match --regex --groups-only '([0-9]+[.][0-9]+([.][0-9]+)?)' -- $shuba_line)
        if test $status -eq 0; and test (count $shuba_match) -ge 1
            printf '%s\n' $shuba_match[1]
            return 0
        end
    end
    return 1
end

function shuba_android_command_line_tools_revision --argument-names shuba_candidate
    if not test -d $shuba_candidate; or test -L $shuba_candidate
        return 1
    end
    set --local shuba_source_properties $shuba_candidate/source.properties
    if not test -f $shuba_source_properties; or test -L $shuba_source_properties
        return 1
    end
    for shuba_tool in $shuba_candidate/bin/apkanalyzer $shuba_candidate/bin/sdkmanager
        if not test -f $shuba_tool; or test -L $shuba_tool; or not test -x $shuba_tool
            return 1
        end
    end
    set --local shuba_versions (sed -n 's/^Pkg[.]Revision[[:space:]]*=[[:space:]]*//p' $shuba_source_properties)
    if test (count $shuba_versions) -ne 1
        return 1
    end
    set --local shuba_version $shuba_versions[1]
    if not string match --regex --quiet '^(0|[1-9][0-9]*)([.](0|[1-9][0-9]*)){1,2}$' -- $shuba_version
        return 1
    end
    printf '%s\n' $shuba_version
end

function shuba_resolve_android_command_line_tools_root
    if not set --query shuba_android_sdk_root
        set --global shuba_android_sdk_root (shuba_resolve_android_sdk_root); or return 1
    end
    set --local shuba_allowed_versions \
        (string split , -- (shuba_contract_get android.command_line_tools_versions)); or return 1
    set --local shuba_tools_parent $shuba_android_sdk_root/cmdline-tools
    for shuba_allowed_version in $shuba_allowed_versions
        set --local shuba_candidates
        set --local shuba_named_candidate $shuba_tools_parent/$shuba_allowed_version
        set --local shuba_named_revision (shuba_android_command_line_tools_revision $shuba_named_candidate)
        if test $status -eq 0; and test "$shuba_named_revision" = "$shuba_allowed_version"
            set --append shuba_candidates $shuba_named_candidate
        end

        set --local shuba_latest_candidate $shuba_tools_parent/latest
        set --local shuba_latest_revision (shuba_android_command_line_tools_revision $shuba_latest_candidate)
        if test $status -eq 0; and test "$shuba_latest_revision" = "$shuba_allowed_version"
            set --append shuba_candidates $shuba_latest_candidate
        end

        if test (count $shuba_candidates) -gt 1
            shuba_fail "multiple Android command-line tools candidates report allowed revision $shuba_allowed_version"
            return 1
        end
        if test (count $shuba_candidates) -eq 1
            set --global shuba_android_command_line_tools_version $shuba_allowed_version
            set --global shuba_android_command_line_tools_root \
                (realpath --canonicalize-existing -- $shuba_candidates[1]); or return 1
            printf '%s\n' $shuba_android_command_line_tools_root
            return 0
        end
    end
    shuba_fail "no allowed Android command-line tools installation is available for "(string join , -- $shuba_allowed_versions)
    return 1
end

function shuba_validate_structured_tools
    set --global shuba_fish_path (realpath --canonicalize-existing -- (status fish-path)); or return 1
    set --global shuba_fish_version $version
    shuba_require_fish_version (shuba_contract_get tool.fish_min_version); or return 1

    set --global shuba_jq_path (shuba_resolve_command jq); or return 1
    set --local shuba_jq_version_output ($shuba_jq_path --version 2>&1)
    if test $status -ne 0; or test (count $shuba_jq_version_output) -ne 1
        shuba_fail 'could not query jq version'
        return 1
    end
    set --global shuba_jq_version (string replace --regex '^jq-' '' -- $shuba_jq_version_output[1])
    shuba_version_at_least $shuba_jq_version (shuba_contract_get tool.jq_min_version); or begin
        shuba_fail "jq "(shuba_contract_get tool.jq_min_version)" or newer is required; selected jq reports $shuba_jq_version"
        return 1
    end

    set --global shuba_xmlstarlet_path (command --search xmlstarlet)
    if test $status -ne 0; or test (count $shuba_xmlstarlet_path) -ne 1
        shuba_fail 'required command is unavailable: xmlstarlet'
        return 1
    end
    if not test -f $shuba_xmlstarlet_path; or not test -x $shuba_xmlstarlet_path
        shuba_fail 'XMLStarlet command is not an executable regular file'
        return 1
    end
    set --local shuba_xmlstarlet_output ($shuba_xmlstarlet_path --version 2>&1)
    if test $status -ne 0
        shuba_fail 'could not query XMLStarlet version'
        return 1
    end
    set --global shuba_xmlstarlet_version unreported
    set --global shuba_xmlstarlet_version_output_sha256 (printf '%s\n' $shuba_xmlstarlet_output | sha256sum | string split --max 1 ' ')[1]
    set --global shuba_xmlstarlet_linked_library_versions \
        (string match --regex '^compiled against .+, linked with .+$' -- $shuba_xmlstarlet_output)
    if test (count $shuba_xmlstarlet_linked_library_versions) -lt 2
        shuba_fail 'XMLStarlet reported no bounded linked-library version evidence'
        return 1
    end
    set --global shuba_xmlstarlet_linked_library_versions_sha256 \
        (printf '%s\n' $shuba_xmlstarlet_linked_library_versions | sha256sum | string split --max 1 ' ')[1]

    set --global shuba_bsdtar_path (shuba_resolve_command bsdtar); or return 1
    set --local shuba_bsdtar_output ($shuba_bsdtar_path --version 2>&1)
    if test $status -ne 0
        shuba_fail 'could not query bsdtar version'
        return 1
    end
    set --global shuba_bsdtar_version (shuba_extract_first_semantic_version $shuba_bsdtar_output); or begin
        shuba_fail 'bsdtar returned no semantic version'
        return 1
    end
    shuba_version_at_least $shuba_bsdtar_version (shuba_contract_get tool.bsdtar_min_version); or begin
        shuba_fail "bsdtar "(shuba_contract_get tool.bsdtar_min_version)" or newer is required; selected bsdtar reports $shuba_bsdtar_version"
        return 1
    end

    printf '{"probe":"ok"}\n' | $shuba_jq_path --exit-status '.probe == "ok"' >/dev/null
    set --local shuba_jq_statuses $pipestatus
    for shuba_status in $shuba_jq_statuses
        if test $shuba_status -ne 0
            shuba_fail 'jq capability probe failed'
            return 1
        end
    end
    printf '<root><probe value="ok"/></root>\n' | $shuba_xmlstarlet_path select --template --value-of '/root/probe/@value' | string match --quiet ok
    set --local shuba_xml_statuses $pipestatus
    for shuba_status in $shuba_xml_statuses
        if test $shuba_status -ne 0
            shuba_fail 'XMLStarlet XPath capability probe failed'
            return 1
        end
    end

    set --local shuba_probe_directory (mktemp --directory /tmp/shuba-structured-tool-probe.XXXXXX); or return 1
    printf 'ok\n' >$shuba_probe_directory/input
    and $shuba_bsdtar_path --create --file $shuba_probe_directory/probe.tar --directory $shuba_probe_directory input >/dev/null 2>&1
    and mkdir $shuba_probe_directory/output
    and $shuba_bsdtar_path --extract --file $shuba_probe_directory/probe.tar --directory $shuba_probe_directory/output >/dev/null 2>&1
    and cmp --silent $shuba_probe_directory/input $shuba_probe_directory/output/input
    set --local shuba_bsdtar_probe_status $status
    rm -rf -- $shuba_probe_directory
    if test $shuba_bsdtar_probe_status -ne 0
        shuba_fail 'bsdtar create/extract capability probe failed'
        return 1
    end
end

function shuba_validate_android_toolchain
    set --global shuba_android_sdk_root (shuba_resolve_android_sdk_root); or return 1
    set --global shuba_android_command_line_tools_root \
        (shuba_resolve_android_command_line_tools_root); or return 1
    set --global shuba_android_command_line_tools_version \
        (shuba_android_command_line_tools_revision $shuba_android_command_line_tools_root); or return 1
    set --global shuba_apkanalyzer_path $shuba_android_command_line_tools_root/bin/apkanalyzer
    set --global shuba_sdkmanager_path $shuba_android_command_line_tools_root/bin/sdkmanager
    shuba_require_executable_file $shuba_apkanalyzer_path; or return 1
    shuba_require_executable_file $shuba_sdkmanager_path; or return 1
    $shuba_apkanalyzer_path --help >/dev/null 2>&1
    if test $status -ne 0
        shuba_fail 'apkanalyzer capability probe failed'
        return 1
    end
    set --local shuba_sdkmanager_version ($shuba_sdkmanager_path --version)
    if test $status -ne 0; or test (count $shuba_sdkmanager_version) -eq 0; or not string match --regex --quiet '^[0-9]+[.][0-9]+([.][0-9]+)?$' -- $shuba_sdkmanager_version[1]
        shuba_fail 'sdkmanager capability probe failed'
        return 1
    end
end

function shuba_resolve_jdk
    set --local shuba_java_candidates
    if set --query JAVA_HOME; and test -n "$JAVA_HOME"
        set --append shuba_java_candidates $JAVA_HOME/bin/java
    end
    set --local shuba_required_java_version (shuba_contract_get android.java_runtime_version)
    for shuba_standard_home in \
        /usr/lib/jvm/java-$shuba_required_java_version-openjdk \
        /usr/lib/jvm/java-$shuba_required_java_version-openjdk-amd64 \
        /usr/lib/jvm/java-$shuba_required_java_version
        if test -x $shuba_standard_home/bin/java
            set --append shuba_java_candidates $shuba_standard_home/bin/java
        end
    end
    set --local shuba_path_java (command --search java 2>/dev/null)
    if test $status -eq 0; and test -n "$shuba_path_java"
        set --append shuba_java_candidates $shuba_path_java
    end
    for shuba_candidate in $shuba_java_candidates
        set --local shuba_java_path (realpath --canonicalize-existing -- $shuba_candidate 2>/dev/null)
        if test $status -ne 0; or not test -x $shuba_java_path
            continue
        end
        set --local shuba_version_output ($shuba_java_path -version 2>&1)
        if test $status -ne 0
            continue
        end
        set --local shuba_major
        for shuba_version_line in $shuba_version_output
            set shuba_major (string match --regex --groups-only 'version "([0-9]+)' -- $shuba_version_line)
            if test (count $shuba_major) -ge 1
                break
            end
        end
        if test (count $shuba_major) -ge 1; and test $shuba_major[1] = $shuba_required_java_version
            set --global shuba_java_path $shuba_java_path
            set --global shuba_java_home (path dirname (path dirname $shuba_java_path))
            set --global shuba_java_version_output (string join ' | ' -- $shuba_version_output)
            set --global shuba_javac_path $shuba_java_home/bin/javac
            shuba_require_executable_file $shuba_javac_path; or return 1
            set --global shuba_javac_version (shuba_first_line $shuba_javac_path --version); or return 1
            if not string match --quiet "javac $shuba_required_java_version*" -- $shuba_javac_version
                shuba_fail "javac reports an incompatible version: $shuba_javac_version"
                return 1
            end
            return 0
        end
    end
    shuba_fail "JDK "(shuba_contract_get android.java_runtime_version)' is required'
    return 1
end

function shuba_validate_android_sdk_layout
    if not set --query shuba_android_sdk_root
        set --global shuba_android_sdk_root (shuba_resolve_android_sdk_root); or return 1
    end
    set --local shuba_platform_root $shuba_android_sdk_root/platforms/android-(shuba_contract_get android.compile_sdk)
    set --local shuba_ndk_root $shuba_android_sdk_root/ndk/(shuba_contract_get android.ndk_version)
    set --local shuba_cmake_root $shuba_android_sdk_root/cmake/(shuba_contract_get android.cmake_version)
    for shuba_file in \
        $shuba_platform_root/android.jar $shuba_platform_root/source.properties \
        $shuba_ndk_root/source.properties $shuba_ndk_root/build/cmake/android.toolchain.cmake
        shuba_require_regular_file $shuba_file; or return 1
    end
    grep --extended-regexp --quiet '^AndroidVersion[.]ApiLevel[[:space:]]*=[[:space:]]*'(shuba_contract_get android.compile_sdk)'$' $shuba_platform_root/source.properties; or begin
        shuba_fail 'Android platform revision differs from the release contract'
        return 1
    end
    grep --fixed-strings --line-regexp --quiet 'Pkg.Revision = '(shuba_contract_get android.ndk_version) $shuba_ndk_root/source.properties; or begin
        shuba_fail 'Android NDK revision differs from the release contract'
        return 1
    end
    for shuba_tool in \
        $shuba_cmake_root/bin/cmake $shuba_cmake_root/bin/ninja \
        $shuba_android_sdk_root/build-tools/(shuba_contract_get android.build_tools_version)/aapt2 \
        $shuba_android_sdk_root/build-tools/(shuba_contract_get android.build_tools_version)/apksigner \
        $shuba_android_sdk_root/build-tools/(shuba_contract_get android.build_tools_version)/zipalign
        shuba_require_executable_file $shuba_tool; or return 1
    end
end

function shuba_resolve_android_verification_tools
    if not set --query shuba_android_sdk_root
        set --global shuba_android_sdk_root (shuba_resolve_android_sdk_root); or return 1
    end
    set --local shuba_build_tools_root $shuba_android_sdk_root/build-tools/(shuba_contract_get android.build_tools_version)
    set --global shuba_aapt2_path $shuba_build_tools_root/aapt2
    set --global shuba_apksigner_path $shuba_build_tools_root/apksigner
    set --global shuba_zipalign_path $shuba_build_tools_root/zipalign
    for shuba_tool in $shuba_aapt2_path $shuba_apksigner_path $shuba_zipalign_path
        shuba_require_executable_file $shuba_tool; or return 1
    end
    set --local shuba_ndk_root $shuba_android_sdk_root/ndk/(shuba_contract_get android.ndk_version)
    shuba_require_regular_file $shuba_ndk_root/source.properties; or return 1
    set --local shuba_ndk_revision (sed -n 's/^Pkg[.]Revision[[:space:]]*=[[:space:]]*//p' $shuba_ndk_root/source.properties)
    if test (count $shuba_ndk_revision) -ne 1; or test $shuba_ndk_revision[1] != (shuba_contract_get android.ndk_version)
        shuba_fail 'resolved NDK revision differs from the release contract'
        return 1
    end
    set --local shuba_llvm_bin $shuba_ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin
    set --global shuba_ndk_readelf_path $shuba_llvm_bin/llvm-readelf
    set --global shuba_ndk_objdump_path $shuba_llvm_bin/llvm-objdump
    if not test -x $shuba_ndk_readelf_path
        shuba_fail "pinned NDK LLVM ELF-header inspector is unavailable: $shuba_ndk_readelf_path"
        return 1
    end
    set --local shuba_ndk_readelf_resolved (realpath --canonicalize-existing -- $shuba_ndk_readelf_path); or return 1
    if not test -f $shuba_ndk_readelf_resolved; or not test -x $shuba_ndk_readelf_resolved
        shuba_fail "pinned NDK LLVM ELF-header inspector resolves to an invalid executable: $shuba_ndk_readelf_path"
        return 1
    end
    if not test -x $shuba_ndk_objdump_path
        shuba_fail "pinned NDK LLVM AArch64 disassembler is unavailable: $shuba_ndk_objdump_path"
        return 1
    end
    set --local shuba_ndk_objdump_resolved (realpath --canonicalize-existing -- $shuba_ndk_objdump_path); or return 1
    if not test -f $shuba_ndk_objdump_resolved; or not test -x $shuba_ndk_objdump_resolved
        shuba_fail "pinned NDK LLVM AArch64 disassembler resolves to an invalid executable: $shuba_ndk_objdump_path"
        return 1
    end
end

function shuba_write_structured_tool_descriptor --argument-names shuba_output_path
    begin
        printf 'tool.fish.path=%s\n' $shuba_fish_path
        printf 'tool.fish.version=%s\n' $shuba_fish_version
        printf 'tool.fish.sha256=%s\n' (shuba_sha256_file $shuba_fish_path)
        printf 'tool.jq.path=%s\n' $shuba_jq_path
        printf 'tool.jq.version=%s\n' $shuba_jq_version
        printf 'tool.jq.sha256=%s\n' (shuba_sha256_file $shuba_jq_path)
        printf 'tool.xmlstarlet.path=%s\n' $shuba_xmlstarlet_path
        printf 'tool.xmlstarlet.version=%s\n' $shuba_xmlstarlet_version
        printf 'tool.xmlstarlet.resolved_path=%s\n' (realpath --canonicalize-existing -- $shuba_xmlstarlet_path)
        printf 'tool.xmlstarlet.sha256=%s\n' (shuba_sha256_file (realpath --canonicalize-existing -- $shuba_xmlstarlet_path))
        printf 'tool.xmlstarlet.version_output_sha256=%s\n' $shuba_xmlstarlet_version_output_sha256
        printf 'tool.xmlstarlet.linked_library_versions_sha256=%s\n' $shuba_xmlstarlet_linked_library_versions_sha256
        printf 'tool.xmlstarlet.xpath_capability=passed\n'
        printf 'tool.bsdtar.path=%s\n' $shuba_bsdtar_path
        printf 'tool.bsdtar.version=%s\n' $shuba_bsdtar_version
        printf 'tool.bsdtar.sha256=%s\n' (shuba_sha256_file $shuba_bsdtar_path)
        if set --query shuba_apkanalyzer_path
            printf 'tool.android_command_line_tools.allowed_versions=%s\n' \
                (shuba_contract_get android.command_line_tools_versions)
            printf 'tool.android_command_line_tools.selected_root=%s\n' \
                $shuba_android_command_line_tools_root
            printf 'tool.android_command_line_tools.selected_version=%s\n' \
                $shuba_android_command_line_tools_version
            printf 'tool.apkanalyzer.path=%s\n' $shuba_apkanalyzer_path
            printf 'tool.apkanalyzer.version=%s\n' $shuba_android_command_line_tools_version
            printf 'tool.apkanalyzer.sha256=%s\n' (shuba_sha256_file $shuba_apkanalyzer_path)
            printf 'tool.sdkmanager.path=%s\n' $shuba_sdkmanager_path
            printf 'tool.sdkmanager.version=%s\n' $shuba_android_command_line_tools_version
            printf 'tool.sdkmanager.sha256=%s\n' (shuba_sha256_file $shuba_sdkmanager_path)
        end
    end >$shuba_output_path
end
