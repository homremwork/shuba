def shuba_fail($message):
  error($message);

if type != "object" then
  shuba_fail("output metadata is not a JSON object")
elif .variantName != "release_Release" then
  "ignore"
elif (.version | type) != "number" or .version != 3 then
  shuba_fail("Release output metadata schema is not version 3")
elif (.artifactType | type) != "object"
     or .artifactType.type != "APK"
     or .artifactType.kind != "Directory" then
  shuba_fail("Release output metadata does not describe an APK directory")
elif (.applicationId | type) != "string"
     or .applicationId == ""
     or .applicationId != $application_id then
  shuba_fail("Release output metadata application ID differs from the release contract")
elif (.elements | type) != "array" or (.elements | length) != 1 then
  shuba_fail("Release output metadata must contain one APK element")
elif (.elements[0] | type) != "object"
     or .elements[0].type != "SINGLE"
     or .elements[0].filters != []
     or .elements[0].attributes != [] then
  shuba_fail("Release output metadata APK element is invalid")
elif (.elements[0].versionCode | type) != "number"
     or (.elements[0].versionCode | floor) != .elements[0].versionCode
     or .elements[0].versionCode != $version_code then
  shuba_fail("Release output metadata version code differs from the release contract")
elif (.elements[0].versionName | type) != "string"
     or .elements[0].versionName == ""
     or .elements[0].versionName != $version_name then
  shuba_fail("Release output metadata version name differs from the release contract")
elif (.elements[0].outputFile | type) != "string"
     or (.elements[0].outputFile | test("^[A-Za-z0-9][A-Za-z0-9._+-]{0,254}[.]apk$")) != true then
  shuba_fail("Release output metadata APK filename is not a bounded ASCII basename")
else
  "release\t\(.elements[0].outputFile)"
end
