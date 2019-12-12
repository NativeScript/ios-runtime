set(WEBKIT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/WebKit")
set(WTF_SOURCE_DIR "${WEBKIT_SOURCE_DIR}/Source/WTF")
set(BMALLOC_SOURCE_DIR "${WEBKIT_SOURCE_DIR}/Source/bmalloc")
set(JavaScriptCore_SOURCE_DIR "${WEBKIT_SOURCE_DIR}/Source/JavaScriptCore")
set(JavaScriptCore_INCLUDE_DIRECTORIES
    "${JavaScriptCore_SOURCE_DIR}/**"
)

set(WEBKIT_CMAKE_ARGS
    --debug=all
    -DCMAKE_SYSTEM_PROCESSOR=arm
    -DCMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS=${CMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS}
    -DCMAKE_XCODE_EFFECTIVE_PLATFORMS=${CMAKE_XCODE_EFFECTIVE_PLATFORMS}
    -DCMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=${CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET}
    -DPORT=Mac
    -DENABLE_INSPECTOR_ALTERNATE_DISPATCHERS=ON
    -DENABLE_REMOTE_INSPECTOR=OFF
    -DENABLE_INTL=OFF
    -DJSC_OBJC_API_ENABLED=OFF
    -DUCONFIG_NO_COLLATION=ON
    -Wno-dev
    -DCMAKE_C_COMPILER_WORKS=YES
    -DCMAKE_CXX_COMPILER_WORKS=YES
    -DHAVE_QOS_CLASSES=ON
    -DENABLE_WEBCORE=OFF
    -DENABLE_WEBKIT_LEGACY=OFF
    -DENABLE_WEBKIT=OFF
    -DENABLE_STATIC_JSC=1
    -DENABLE_JIT=ON
    -DENABLE_EXCEPTION_SCOPE_VERIFICATION=ON
    -DUSE_CAPSTONE=ON
    -DCMAKE_C_COMPILER_ID=${CMAKE_C_COMPILER_ID}
    -DCMAKE_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_CXX_STANDARD_REQUIRED=ON
    # Override RelWithDebInfo configuration's defaults to the ones from NativeScript
    -DCMAKE_CXX_FLAGS_RELWITHDEBINFO=${CMAKE_CXX_FLAGS_RELWITHDEBINFO}

)

include(ExternalProject)
ExternalProject_Add(
    WebKit
    SOURCE_DIR ${WEBKIT_SOURCE_DIR}
    CONFIGURE_COMMAND env -i "${CMAKE_COMMAND}" ${WEBKIT_CMAKE_ARGS} -G${CMAKE_GENERATOR} ${WEBKIT_SOURCE_DIR}
    BUILD_ALWAYS 1
    BUILD_COMMAND ${CMAKE_SOURCE_DIR}/build/scripts/build-step-webkit.sh
    INSTALL_COMMAND ""
)

include(SetActiveArchitectures)
SetActiveArchitectures(WebKit)

get_property(WEBKIT_BINARY_DIR TARGET WebKit PROPERTY _EP_BINARY_DIR)

set(WEBKIT_INCLUDE_DIRECTORIES
    "${WEBKIT_SOURCE_DIR}/Source"
    "${WTF_SOURCE_DIR}"
    "${WTF_SOURCE_DIR}/icu"
    "${BMALLOC_SOURCE_DIR}"
    ${JavaScriptCore_INCLUDE_DIRECTORIES}
    "${WEBKIT_BINARY_DIR}"
    "${WEBKIT_BINARY_DIR}/DerivedSources"
    "${WEBKIT_BINARY_DIR}/DerivedSources/ForwardingHeaders"
    "${WEBKIT_BINARY_DIR}/DerivedSources/JavaScriptCore"
    "${WEBKIT_BINARY_DIR}/DerivedSources/JavaScriptCore/inspector"
)

set(WEBKIT_LINK_DIRECTORIES "${WEBKIT_BINARY_DIR}/lib-$(PLATFORM_NAME)/$(CONFIGURATION)")
set(WEBKIT_LIBRARIES bmalloc WTF JavaScriptCore)

add_definitions(-DBUILDING_WITH_CMAKE=1 -DHAVE_CONFIG_H=1 -DSTATICALLY_LINKED_WITH_WTF)
