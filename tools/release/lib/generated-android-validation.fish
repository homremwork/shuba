function shuba_generated_xml_value --argument-names shuba_xml_path shuba_xpath
    set --local shuba_diagnostic (mktemp /tmp/shuba-xmlstarlet.XXXXXX); or return 1
    set --local shuba_output (mktemp /tmp/shuba-xmlstarlet-output.XXXXXX); or begin
        rm -f -- $shuba_diagnostic
        return 1
    end
    $shuba_xmlstarlet_path select \
        --namespace android=http://schemas.android.com/apk/res/android \
        --template --output __SHUBA_XML_VALUE__ --value-of $shuba_xpath \
        $shuba_xml_path >$shuba_output 2>$shuba_diagnostic
    set --local shuba_xml_status $status
    set --local shuba_encoded_value (string collect <$shuba_output)
    if test $shuba_xml_status -ne 0
        set --local shuba_detail (string join ' ' -- (string trim <$shuba_diagnostic))
        rm -f -- $shuba_diagnostic $shuba_output
        shuba_fail "XML query failed for $shuba_xml_path: $shuba_detail"
        return 1
    end
    if not string match --quiet '__SHUBA_XML_VALUE__*' -- $shuba_encoded_value
        rm -f -- $shuba_diagnostic $shuba_output
        shuba_fail "XML query returned an invalid bounded record for $shuba_xml_path"
        return 1
    end
    set --local shuba_value (string replace __SHUBA_XML_VALUE__ '' -- $shuba_encoded_value | string collect)
    rm -f -- $shuba_diagnostic $shuba_output
    printf '%s' $shuba_value
end

function shuba_generated_require_xml_value --argument-names shuba_xml_path shuba_xpath shuba_expected shuba_message
    set --local shuba_actual (shuba_generated_xml_value $shuba_xml_path $shuba_xpath); or return 1
    if test "$shuba_actual" != "$shuba_expected"
        shuba_fail $shuba_message
        return 1
    end
end

function shuba_generated_require_fixed_count --argument-names shuba_file_path shuba_needle shuba_expected_count shuba_message
    shuba_require_regular_file $shuba_file_path; or return 1
    set --local shuba_pattern (string escape --style=regex -- $shuba_needle)
    set --local shuba_matches (string match --all --regex -- $shuba_pattern <$shuba_file_path)
    if test (count $shuba_matches) -ne $shuba_expected_count
        shuba_fail $shuba_message
        return 1
    end
end

function shuba_generated_require_exact_inventory --argument-names shuba_root shuba_output_path
    set --erase argv[1..2]
    if not test -d $shuba_root; or test -L $shuba_root
        shuba_fail "inventory root is unavailable or symbolic-linked: $shuba_root"
        return 1
    end
    set --local shuba_paths_file (mktemp /tmp/shuba-generated-paths.XXXXXX); or return 1
    find -P $shuba_root -type l -print0 | read --null shuba_link_path
    set --local shuba_link_statuses $pipestatus
    if test $shuba_link_statuses[1] -ne 0
        rm -f -- $shuba_paths_file
        shuba_fail "could not inspect generated links below $shuba_root"
        return 1
    end
    if test (count $shuba_link_path) -ne 0
        rm -f -- $shuba_paths_file
        shuba_fail "generated inventory contains a symbolic link: $shuba_link_path"
        return 1
    end
    find -P $shuba_root -type f -print0 | sort --zero-terminated >$shuba_paths_file
    set --local shuba_find_statuses $pipestatus
    for shuba_status in $shuba_find_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_paths_file
            shuba_fail "could not enumerate generated inventory below $shuba_root"
            return 1
        end
    end
    set --local shuba_actual_paths
    while read --null shuba_path
        set --local shuba_relative_path (string replace "$shuba_root/" '' -- $shuba_path | string collect)
        if string match --regex --quiet '[[:cntrl:]]' -- $shuba_relative_path
            rm -f -- $shuba_paths_file
            shuba_fail 'generated inventory contains a control character'
            return 1
        end
        set --append shuba_actual_paths $shuba_relative_path
    end <$shuba_paths_file
    rm -f -- $shuba_paths_file
    set --local shuba_expected_paths (printf '%s\n' $argv | sort)
    set --local shuba_actual_descriptor (string join \n -- $shuba_actual_paths)
    set --local shuba_expected_descriptor (string join \n -- $shuba_expected_paths)
    if test "$shuba_actual_descriptor" != "$shuba_expected_descriptor"
        shuba_fail "generated file inventory changed below $shuba_root"
        return 1
    end
    printf '%s\n' $shuba_actual_paths >$shuba_output_path
end

function shuba_generated_check_project_authority --argument-names shuba_root shuba_state_root
    set --local shuba_project $shuba_root/Shuba.jucer
    shuba_require_regular_file $shuba_project; or return 1
    $shuba_xmlstarlet_path validate --well-formed --quiet $shuba_project >/dev/null 2>&1
    if test $status -ne 0
        shuba_fail 'Shuba.jucer is not well-formed XML'
        return 1
    end
    shuba_generated_require_xml_value $shuba_project 'name(/*)' JUCERPROJECT 'Shuba.jucer has an unexpected root element'; or return 1
    for shuba_record in \
        'name|app.name' \
        'bundleIdentifier|app.application_id' \
        'version|app.version_name'
        set --local shuba_fields (string split '|' -- $shuba_record)
        shuba_generated_require_xml_value $shuba_project "string(/JUCERPROJECT/@$shuba_fields[1])" (shuba_contract_get $shuba_fields[2]) "Shuba.jucer $shuba_fields[1] differs from $shuba_fields[2]"; or return 1
    end
    for shuba_record in \
        'cppLanguageStandard|23' \
        'useGNUExtensions|0' \
        'binaryDataNamespace|ShubaBinaryData'
        set --local shuba_fields (string split '|' -- $shuba_record)
        shuba_generated_require_xml_value $shuba_project "string(/JUCERPROJECT/@$shuba_fields[1])" $shuba_fields[2] "Shuba.jucer has an invalid $shuba_fields[1]"; or return 1
    end
    set --local shuba_exporter /JUCERPROJECT/EXPORTFORMATS/ANDROIDSTUDIO
    shuba_generated_require_xml_value $shuba_project "count($shuba_exporter)" 1 'expected exactly one Android exporter'; or return 1
    for shuba_record in \
        'targetFolder|Builds/Android' \
        'androidVersionCode|app.version_code' \
        'androidMinimumSDK|android.min_sdk' \
        'androidTargetSDK|android.target_sdk'
        set --local shuba_fields (string split '|' -- $shuba_record)
        set --local shuba_expected $shuba_fields[2]
        if string match --quiet '*.*' -- $shuba_expected
            set shuba_expected (shuba_contract_get $shuba_expected); or return 1
        end
        shuba_generated_require_xml_value $shuba_project "string($shuba_exporter/@$shuba_fields[1])" $shuba_expected "Android exporter $shuba_fields[1] differs from authority"; or return 1
    end
    for shuba_attribute in \
        androidBluetoothAdvertiseNeeded androidBluetoothConnectNeeded androidBluetoothScanNeeded \
        androidEnableContentSharing androidEnableRemoteNotifications androidExternalWriteNeeded \
        androidInAppBilling androidInternetNeeded androidPushNotifications androidReadMediaAudioPermission \
        androidReadMediaImagesPermission androidReadMediaVideoPermission androidVibratePermissionNeeded \
        androidEnableVirtualMidi cameraPermissionNeeded microphonePermissionNeeded
        shuba_generated_require_xml_value $shuba_project "string($shuba_exporter/@$shuba_attribute)" 0 "Android exporter does not disable $shuba_attribute"; or return 1
    end
    for shuba_record in \
        'androidOtherPermissions|' \
        'smallIcon|appiconsmall01' \
        'bigIcon|appiconlarge01' \
        'androidAdditionalResourceFolders|Assets/Android/res'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_generated_require_xml_value $shuba_project "string($shuba_exporter/@$shuba_fields[1])" "$shuba_fields[2]" "Android exporter has an invalid $shuba_fields[1]"; or return 1
    end
    set --local shuba_gradle_authority (shuba_generated_xml_value $shuba_project "string($shuba_exporter/@androidCustomAppBuildGradleContent)"); or return 1
    string match --quiet "*ndkVersion = \""(shuba_contract_get android.ndk_version)"\"*" -- $shuba_gradle_authority; or begin
        shuba_fail 'Android exporter does not pin the release-contract NDK'
        return 1
    end
    string match --quiet "*buildToolsVersion = \""(shuba_contract_get android.build_tools_version)"\"*" -- $shuba_gradle_authority; or begin
        shuba_fail 'Android exporter does not pin the release-contract build tools'
        return 1
    end

    set --local shuba_configurations "$shuba_exporter/CONFIGURATIONS/CONFIGURATION"
    shuba_generated_require_xml_value $shuba_project "count($shuba_configurations)" 2 'expected Debug and Release Android configurations'; or return 1
    for shuba_record in 'Debug|1' 'Release|0'
        set --local shuba_fields (string split '|' -- $shuba_record)
        set --local shuba_configuration (string join '' -- $shuba_configurations "[@name='" $shuba_fields[1] "']")
        shuba_generated_require_xml_value $shuba_project "count($shuba_configuration)" 1 "missing or duplicate $shuba_fields[1] Android configuration"; or return 1
        shuba_generated_require_xml_value $shuba_project "string($shuba_configuration/@isDebug)" $shuba_fields[2] "$shuba_fields[1] debug classification is wrong"; or return 1
        shuba_generated_require_xml_value $shuba_project "string($shuba_configuration/@targetName)" (shuba_contract_get app.name) "$shuba_fields[1] target name is wrong"; or return 1
        shuba_generated_require_xml_value $shuba_project "string($shuba_configuration/@androidArchitectures)" (shuba_contract_get android.abi) "$shuba_fields[1] ABI is wrong"; or return 1
        shuba_generated_require_xml_value $shuba_project "string($shuba_configuration/@libraryPath)" '../../build/libjxl-android-arm64/lib;../../build/libjxl-android-arm64/third_party/brotli;../../build/libjxl-android-arm64/third_party/highway' "$shuba_fields[1] library paths are incomplete or reordered"; or return 1
    end
    set --local shuba_resources '//FILE[@resource="1"]'
    shuba_generated_require_xml_value $shuba_project "count($shuba_resources)" 1 'expected exactly one BinaryData resource'; or return 1
    shuba_generated_require_xml_value $shuba_project "string($shuba_resources/@file)" Localization/ru.po 'unexpected BinaryData resource'; or return 1
    shuba_generated_require_xml_value $shuba_project "string($shuba_resources/@id)" russianproductioncatalog 'unexpected Russian resource ID'; or return 1

    set --local shuba_source_xpath '//FILE[starts-with(@file,"Source/")]'
    set --local shuba_unsafe_source_xpath (string join '' -- 'count(' $shuba_source_xpath "[string-length(translate(@file,'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./+-','')) != 0])" | string collect)
    shuba_generated_require_xml_value $shuba_project "$shuba_unsafe_source_xpath" 0 'Shuba.jucer contains an unsafe source path'; or return 1
    set --local shuba_project_sources ($shuba_xmlstarlet_path select --template --match $shuba_source_xpath --value-of @file --nl $shuba_project)
    if test $status -ne 0
        shuba_fail 'could not extract Shuba.jucer source inventory'
        return 1
    end
    printf '%s\n' $shuba_project_sources | sort >$shuba_state_root/project-sources
    if test (count $shuba_project_sources) -ne (count (sort --unique $shuba_state_root/project-sources))
        shuba_fail 'Shuba.jucer contains a duplicate source entry'
        return 1
    end

    set --local shuba_disk_paths $shuba_state_root/source.paths
    find -P $shuba_root/Source -type f -print0 | sort --zero-terminated >$shuba_disk_paths
    set --local shuba_find_statuses $pipestatus
    for shuba_status in $shuba_find_statuses
        if test $shuba_status -ne 0
            shuba_fail 'could not enumerate production source inventory'
            return 1
        end
    end
    set --local shuba_disk_sources
    while read --null shuba_source_path
        set --local shuba_relative_path (string replace "$shuba_root/" '' -- $shuba_source_path | string collect)
        if contains -- $shuba_relative_path Source/Platform/LinuxFakes.cpp Source/Platform/LinuxFakes.hpp
            continue
        end
        if string match --regex --quiet '[[:cntrl:]]' -- $shuba_relative_path
            shuba_fail 'production source inventory contains a control character'
            return 1
        end
        set --append shuba_disk_sources $shuba_relative_path
    end <$shuba_disk_paths
    printf '%s\n' $shuba_disk_sources | sort >$shuba_state_root/disk-sources
    if not cmp --silent $shuba_state_root/project-sources $shuba_state_root/disk-sources
        shuba_fail 'Shuba.jucer Android source inventory differs from production sources'
        return 1
    end
    set --global shuba_generated_source_count (count $shuba_project_sources)
end

function shuba_generated_check_file_inventories --argument-names shuba_root shuba_state_root
    set --local shuba_android_root $shuba_root/Builds/Android
    set --local shuba_android_stage $shuba_state_root/android-inventory
    mkdir $shuba_android_stage; or return 1
    for shuba_path in \
        app/build.gradle app/CMakeLists.txt app/src/debug/res/values/string.xml \
        app/src/main/AndroidManifest.xml app/src/main/res/drawable-hdpi/icon.png \
        app/src/main/res/drawable-ldpi/icon.png app/src/main/res/drawable-mdpi/icon.png \
        app/src/main/res/drawable-xhdpi/icon.png app/src/release/res/values/string.xml \
        build.gradle gradle.properties gradle/wrapper/gradle-wrapper.jar \
        gradle/wrapper/gradle-wrapper.properties gradle/wrapper/LICENSE-for-gradlewrapper.txt \
        gradlew gradlew.bat local.properties settings.gradle
        set --local shuba_source $shuba_android_root/$shuba_path
        shuba_require_regular_file $shuba_source; or return 1
        mkdir -p $shuba_android_stage/(path dirname $shuba_path); or return 1
        cp -- $shuba_source $shuba_android_stage/$shuba_path; or return 1
    end
    shuba_generated_require_exact_inventory $shuba_android_stage $shuba_state_root/android-files \
        app/CMakeLists.txt app/build.gradle app/src/debug/res/values/string.xml \
        app/src/main/AndroidManifest.xml app/src/main/res/drawable-hdpi/icon.png \
        app/src/main/res/drawable-ldpi/icon.png app/src/main/res/drawable-mdpi/icon.png \
        app/src/main/res/drawable-xhdpi/icon.png app/src/release/res/values/string.xml \
        build.gradle gradle.properties gradle/wrapper/LICENSE-for-gradlewrapper.txt \
        gradle/wrapper/gradle-wrapper.jar gradle/wrapper/gradle-wrapper.properties \
        gradlew gradlew.bat local.properties settings.gradle; or return 1

    shuba_generated_require_exact_inventory $shuba_root/JuceLibraryCode $shuba_state_root/juce-files \
        BinaryData.cpp BinaryData.h JuceHeader.h ReadMe.txt include_juce_core.cpp \
        include_juce_core.mm include_juce_core_CompilationTime.cpp include_juce_core_zlib.c include_juce_cryptography.cpp \
        include_juce_cryptography.mm include_juce_data_structures.cpp include_juce_data_structures.mm \
        include_juce_events.cpp include_juce_events.mm include_juce_graphics.cpp \
        include_juce_graphics.mm include_juce_graphics_Harfbuzz.cpp include_juce_graphics_libjpg_1.c \
        include_juce_graphics_libjpg_2.c include_juce_graphics_libjpg_3.c include_juce_graphics_libpng.c \
        include_juce_graphics_lunasvg.c \
        include_juce_graphics_Sheenbidi.c include_juce_gui_basics.cpp include_juce_gui_basics.mm \
        include_juce_gui_basics_2.cpp include_juce_gui_basics_3.cpp include_juce_gui_basics_4.cpp \
        include_juce_gui_basics_5.cpp; or return 1

    set --local shuba_unexpected_path (find -P $shuba_android_root \
        \( -path $shuba_android_root/.gradle -o -path $shuba_android_root/app/.cxx \
        -o -path $shuba_android_root/app/build -o -path $shuba_android_root/build \) -prune \
        -o -type f -print0 | sort --zero-terminated | while read --null shuba_path
            set --local shuba_relative (string replace "$shuba_android_root/" '' -- $shuba_path | string collect)
            if not grep --fixed-strings --line-regexp --quiet -- $shuba_relative $shuba_state_root/android-files
                printf '%s\n' $shuba_relative
                break
            end
        end)
    if test -n "$shuba_unexpected_path"
        shuba_fail "generated Android inventory contains an unexpected file: $shuba_unexpected_path"
        return 1
    end
end

function shuba_generated_check_gradle --argument-names shuba_root
    set --local shuba_android_root $shuba_root/Builds/Android
    set --local shuba_app_gradle $shuba_android_root/app/build.gradle
    set --local shuba_build_gradle $shuba_android_root/build.gradle
    set --local shuba_settings $shuba_android_root/settings.gradle
    set --local shuba_wrapper $shuba_android_root/gradle/wrapper/gradle-wrapper.properties
    for shuba_record in \
        "$shuba_build_gradle|classpath 'com.android.tools.build:gradle:"(shuba_contract_get android.gradle_plugin_version)"'|1" \
        "$shuba_app_gradle|compileSdk("(shuba_contract_get android.compile_sdk)")|1" \
        "$shuba_app_gradle|minSdkVersion("(shuba_contract_get android.min_sdk)")|1" \
        "$shuba_app_gradle|targetSdkVersion("(shuba_contract_get android.target_sdk)")|1" \
        "$shuba_app_gradle|applicationId(\""(shuba_contract_get app.application_id)"\")|1" \
        "$shuba_app_gradle|namespace = \""(shuba_contract_get app.application_id)"\"|1" \
        "$shuba_app_gradle|version = \""(shuba_contract_get android.cmake_version)"\"|1" \
        "$shuba_app_gradle|ndkVersion = \""(shuba_contract_get android.ndk_version)"\"|1" \
        "$shuba_app_gradle|buildToolsVersion = \""(shuba_contract_get android.build_tools_version)"\"|1" \
        "$shuba_app_gradle|abiFilters(\""(shuba_contract_get android.abi)"\")|2" \
        "$shuba_app_gradle|\"-DCMAKE_CXX_STANDARD=23\"|1" \
        "$shuba_app_gradle|\"-DCMAKE_CXX_EXTENSIONS=OFF\"|1" \
        "$shuba_app_gradle|arguments(\"-DJUCE_BUILD_CONFIGURATION=DEBUG\")|1" \
        "$shuba_app_gradle|arguments(\"-DJUCE_BUILD_CONFIGURATION=RELEASE\")|1" \
        "$shuba_app_gradle|../../../Assets/Android/res|1" \
        "$shuba_settings|rootProject.name = '"(shuba_contract_get app.name)"'|1" \
        "$shuba_settings|include ':app'|1" \
        "$shuba_settings|id 'org.gradle.toolchains.foojay-resolver-convention' version '1.0.0'|1" \
        "$shuba_wrapper|gradle-"(shuba_contract_get android.gradle_version)"-all.zip|1"
        set --local shuba_fields (string split --max 2 '|' -- $shuba_record)
        shuba_generated_require_fixed_count $shuba_fields[1] $shuba_fields[2] $shuba_fields[3] "generated Gradle contract check failed for $shuba_fields[2]"; or return 1
    end
    shuba_generated_require_fixed_count $shuba_app_gradle 'abiFilters("' 2 'unexpected generated ABI filter inventory'; or return 1
    if test (cat $shuba_android_root/gradle.properties | string collect) != 'android.useAndroidX=true'
        shuba_fail 'generated gradle.properties changed unexpectedly'
        return 1
    end
    if not test -x $shuba_android_root/gradlew
        shuba_fail 'generated Gradle wrapper is not executable'
        return 1
    end
end

function shuba_generated_check_manifest --argument-names shuba_root
    set --local shuba_manifest $shuba_root/Builds/Android/app/src/main/AndroidManifest.xml
    $shuba_xmlstarlet_path validate --well-formed --quiet $shuba_manifest >/dev/null 2>&1; or begin
        shuba_fail 'generated Android manifest is not well-formed XML'
        return 1
    end
    for shuba_record in \
        'name(/*)|manifest' \
        'string(/manifest/@android:versionCode)|app.version_code' \
        'string(/manifest/@android:versionName)|app.version_name'
        set --local shuba_fields (string split '|' -- $shuba_record)
        set --local shuba_expected $shuba_fields[2]
        if string match --quiet '*.*' -- $shuba_expected
            set shuba_expected (shuba_contract_get $shuba_expected); or return 1
        end
        shuba_generated_require_xml_value $shuba_manifest $shuba_fields[1] $shuba_expected 'generated manifest identity differs from the release contract'; or return 1
    end
    shuba_generated_require_xml_value $shuba_manifest 'count(/manifest/uses-permission)' 0 'generated manifest requests unapproved permissions'; or return 1
    set --local shuba_application /manifest/application
    shuba_generated_require_xml_value $shuba_manifest "count($shuba_application)" 1 'expected one application element'; or return 1
    for shuba_record in \
        'string(/manifest/application/@android:label)|@string/app_name' \
        'string(/manifest/application/@android:icon)|@drawable/icon' \
        'count(/manifest/application/activity)|1' \
        'string(/manifest/application/activity/@android:name)|android.app.Activity' \
        'string(/manifest/application/activity/@android:exported)|true' \
        'count(/manifest/application/activity/intent-filter)|1' \
        'count(/manifest/application/activity/intent-filter/action)|1' \
        'string(/manifest/application/activity/intent-filter/action/@android:name)|android.intent.action.MAIN' \
        'count(/manifest/application/activity/intent-filter/category)|1' \
        'string(/manifest/application/activity/intent-filter/category/@android:name)|android.intent.category.LAUNCHER' \
        'count(/manifest/application/receiver)|1' \
        'string(/manifest/application/receiver/@android:exported)|false' \
        'count(/manifest/application/service) + count(/manifest/application/provider)|0'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_generated_require_xml_value $shuba_manifest $shuba_fields[1] "$shuba_fields[2]" "generated manifest component contract failed: $shuba_fields[1]"; or return 1
    end
end

function shuba_generated_check_native_cmake --argument-names shuba_root shuba_state_root
    set --local shuba_cmake $shuba_root/Builds/Android/app/CMakeLists.txt
    for shuba_record in \
        'cmake_minimum_required(VERSION 3.22)|1' \
        'JUCE_ANDROID_API_VERSION='(shuba_contract_get android.target_sdk)'|1' \
        'JUCE_APP_VERSION='(shuba_contract_get app.version_name)'|3' \
        'JUCE_PROJUCER_VERSION=0x90001|2' \
        '[[-DNDEBUG=1]]|1' \
        '[[-DDEBUG=1]]|1' \
        'if(JUCE_BUILD_CONFIGURATION MATCHES "RELEASE")|1'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_generated_require_fixed_count $shuba_cmake $shuba_fields[1] $shuba_fields[2] "generated native CMake contract failed for $shuba_fields[1]"; or return 1
    end
    sed -n 's@^[[:space:]]*"\.\./\.\./\.\./Source/\([^"[:cntrl:]]*\)"[[:space:]]*$@Source/\1@p' $shuba_cmake | sort --unique >$shuba_state_root/cmake-sources
    set --local shuba_source_statuses $pipestatus
    for shuba_status in $shuba_source_statuses
        test $shuba_status -eq 0; or return 1
    end
    if not cmp --silent $shuba_state_root/project-sources $shuba_state_root/cmake-sources
        shuba_fail 'generated native source inventory differs from Shuba.jucer'
        return 1
    end
    for shuba_path in \
        ../../../Source ../../../third_party/glaze/include ../../../third_party/spiritless_po/include \
        ../../../build/libjxl-android-arm64/lib/include
        shuba_generated_require_fixed_count $shuba_cmake "\"$shuba_path\"" 2 "generated include path inventory is wrong for $shuba_path"; or return 1
    end
    for shuba_path in \
        ../../../build/libjxl-android-arm64/lib \
        ../../../build/libjxl-android-arm64/third_party/brotli \
        ../../../build/libjxl-android-arm64/third_party/highway
        shuba_generated_require_fixed_count $shuba_cmake "\"$shuba_path\"" 22 "generated library path inventory is wrong for $shuba_path"; or return 1
    end
    for shuba_wrapper in \
        include_juce_core_zlib.c include_juce_graphics_libjpg_1.c include_juce_graphics_libjpg_2.c \
        include_juce_graphics_libjpg_3.c include_juce_graphics_libpng.c include_juce_graphics_lunasvg.c \
        include_juce_graphics_Sheenbidi.c
        shuba_generated_require_fixed_count $shuba_cmake "\"../../../JuceLibraryCode/$shuba_wrapper\"" 1 "generated native C source inventory is wrong for $shuba_wrapper"; or return 1
    end
    sed -n 's/^[[:space:]]*\[\[\([A-Za-z0-9_]*\)\]\][[:space:]]*$/\1/p' $shuba_cmake >$shuba_state_root/link-libraries
    if test (cat $shuba_state_root/link-libraries | string join ,) != 'jxl_dec,jxl,jxl_threads,jxl_cms,brotlienc,brotlidec,brotlicommon,hwy,jnigraphics,m'
        shuba_fail 'generated native link library order changed'
        return 1
    end
end

function shuba_generated_check_binary_data --argument-names shuba_root
    set --local shuba_source $shuba_root/Localization/ru.po
    set --local shuba_header $shuba_root/JuceLibraryCode/BinaryData.h
    set --local shuba_implementation $shuba_root/JuceLibraryCode/BinaryData.cpp
    set --local shuba_size (stat --format %s -- $shuba_source); or return 1
    for shuba_record in \
        'namespace ShubaBinaryData|1' \
        'extern const char*   ru_po;|1' \
        "const int            ru_poSize = $shuba_size;|1" \
        'const int namedResourceListSize = 1;|1'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_generated_require_fixed_count $shuba_header $shuba_fields[1] $shuba_fields[2] "generated BinaryData header contract failed for $shuba_fields[1]"; or return 1
    end
    for shuba_record in \
        '//================== ru.po ==================|1' \
        '"ru_po"|1' \
        '"ru.po"|1' \
        "numBytes = $shuba_size; return ru_po;|1"
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_generated_require_fixed_count $shuba_implementation $shuba_fields[1] $shuba_fields[2] "generated BinaryData implementation contract failed for $shuba_fields[1]"; or return 1
    end
    set --local shuba_source_bytes (od --address-radix=n --format=u1 --read-bytes=48 $shuba_source)
    if test $status -ne 0
        shuba_fail 'could not inspect localization source bytes'
        return 1
    end
    set --local shuba_prefix_numbers (string match --all --regex '[0-9]+' -- (string join ' ' -- $shuba_source_bytes))
    set --local shuba_source_prefix (string join , -- $shuba_prefix_numbers)','
    set --local shuba_compact_implementation (string replace --all --regex '[[:space:]]+' '' < $shuba_implementation | string collect)
    if not string match --quiet "*$shuba_source_prefix*" -- $shuba_compact_implementation
        shuba_fail 'generated ru.po payload prefix is stale or corrupt'
        return 1
    end
end

function shuba_generated_check_png --argument-names shuba_png_path shuba_expected_width shuba_expected_height
    shuba_require_regular_file $shuba_png_path; or return 1
    set --local shuba_png_size (stat --format %s -- $shuba_png_path); or return 1
    if test $shuba_png_size -lt 24
        shuba_fail "generated icon is too short to contain a PNG header: $shuba_png_path"
        return 1
    end
    set --local shuba_signature (od --address-radix=n --format=x1 --read-bytes=8 $shuba_png_path | string replace --all --regex '[[:space:]]' '' | string collect)
    set --local shuba_chunk (od --address-radix=n --format=c --skip-bytes=12 --read-bytes=4 $shuba_png_path | string replace --all --regex '[[:space:]]' '' | string collect)
    set --local shuba_dimensions (od --address-radix=n --format=u4 --endian=big --skip-bytes=16 --read-bytes=8 $shuba_png_path)
    if test "$shuba_signature" != 89504e470d0a1a0a; or test "$shuba_chunk" != IHDR
        shuba_fail "generated icon is not a PNG with IHDR: $shuba_png_path"
        return 1
    end
    set shuba_dimensions (string match --all --regex '[0-9]+' -- (string join ' ' -- $shuba_dimensions))
    if test (count $shuba_dimensions) -ne 2; or test $shuba_dimensions[1] -ne $shuba_expected_width; or test $shuba_dimensions[2] -ne $shuba_expected_height
        shuba_fail "generated icon has wrong dimensions: $shuba_png_path"
        return 1
    end
end

function shuba_generated_check_header_icons_sdk --argument-names shuba_root
    set --local shuba_header $shuba_root/JuceLibraryCode/JuceHeader.h
    for shuba_record in \
        'projectName    = "'(shuba_contract_get app.name)'";|1' \
        'versionString  = "'(shuba_contract_get app.version_name)'";|1'
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        shuba_generated_require_fixed_count $shuba_header $shuba_fields[1] $shuba_fields[2] "generated JuceHeader identity failed for $shuba_fields[1]"; or return 1
    end
    set --local shuba_modules (sed -n 's@^#include <\([^/]*\)/[^>]*>$@\1@p' $shuba_header | sort)
    if test (string join , -- $shuba_modules) != 'juce_core,juce_cryptography,juce_data_structures,juce_events,juce_graphics,juce_gui_basics'
        shuba_fail 'generated JUCE module inventory changed'
        return 1
    end
    set --local shuba_resource_root $shuba_root/Builds/Android/app/src/main/res
    for shuba_record in \
        'drawable-ldpi/icon.png|36|36' \
        'drawable-mdpi/icon.png|48|48' \
        'drawable-hdpi/icon.png|72|72' \
        'drawable-xhdpi/icon.png|96|96'
        set --local shuba_fields (string split '|' -- $shuba_record)
        shuba_generated_check_png $shuba_resource_root/$shuba_fields[1] $shuba_fields[2] $shuba_fields[3]; or return 1
    end
    set --local shuba_icon_paths (find -P $shuba_resource_root -type f -name icon.png -printf '%P\n' | sort)
    if test (string join , -- $shuba_icon_paths) != 'drawable-hdpi/icon.png,drawable-ldpi/icon.png,drawable-mdpi/icon.png,drawable-xhdpi/icon.png'
        shuba_fail 'generated icon inventory changed'
        return 1
    end
    set --local shuba_sdk_lines (string match --regex --groups-only '^sdk[.]dir=(/[^[:cntrl:]]+)$' <$shuba_root/Builds/Android/local.properties)
    if test (count $shuba_sdk_lines) -ne 1; or not test -d $shuba_sdk_lines[1]; or not test -x $shuba_sdk_lines[1]/platform-tools/adb
        shuba_fail 'generated SDK path is invalid or has no platform-tools/adb'
        return 1
    end
end

function shuba_validate_generated_android --argument-names shuba_root
    if not test -d $shuba_root; or test -L $shuba_root
        shuba_fail 'generated Android validation root must be a non-symbolic-link directory'
        return 1
    end
    set --local shuba_state_root (mktemp --directory /tmp/shuba-generated-validation.XXXXXX); or return 1
    shuba_generated_check_project_authority $shuba_root $shuba_state_root
    and shuba_generated_check_file_inventories $shuba_root $shuba_state_root
    and shuba_generated_check_gradle $shuba_root
    and shuba_generated_check_manifest $shuba_root
    and shuba_generated_check_native_cmake $shuba_root $shuba_state_root
    and shuba_generated_check_binary_data $shuba_root
    and shuba_generated_check_header_icons_sdk $shuba_root
    set --local shuba_validation_status $status
    rm -rf -- $shuba_state_root
    if test $shuba_validation_status -ne 0
        return $shuba_validation_status
    end
    printf 'generated Android check: %s %s, API %s, %s, JUCE e18f7f506c0b, %s source files, zero requested permissions, and generated resources are consistent.\n' \
        (shuba_contract_get app.name) \
        (shuba_contract_get app.version_name) \
        (shuba_contract_get android.target_sdk) \
        (shuba_contract_get android.abi) \
        $shuba_generated_source_count
end
