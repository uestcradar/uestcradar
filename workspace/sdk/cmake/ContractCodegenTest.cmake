if(NOT DEFINED CODEGEN OR NOT DEFINED SDK_SOURCE_DIR OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "CODEGEN, SDK_SOURCE_DIR and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/valid")

execute_process(
    COMMAND "${CODEGEN}"
        "${SDK_SOURCE_DIR}/include/contract_catalog.def"
        "${SDK_SOURCE_DIR}/contracts"
        "${TEST_ROOT}/valid"
    RESULT_VARIABLE valid_result
    ERROR_VARIABLE valid_error)
if(NOT valid_result EQUAL 0)
    message(FATAL_ERROR "valid contracts were rejected: ${valid_error}")
endif()

file(READ "${TEST_ROOT}/valid/contracts.manifest.json" manifest)
foreach(required
    "\"name\": \"iq\""
    "\"name\": \"pulse_compression\""
    "\"name\": \"rd\""
    "\"kind\": \"heatmap\"")
    string(FIND "${manifest}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "generated manifest is missing ${required}")
    endif()
endforeach()

file(READ "${TEST_ROOT}/valid/contracts.generated.go" go_decoder)
foreach(required "DecodeMetadata" "payload length mismatch" "LittleEndian")
    string(FIND "${go_decoder}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "generated Go decoder is missing ${required}")
    endif()
endforeach()
file(READ "${TEST_ROOT}/valid/contracts.generated.ts" ts_decoder)
foreach(required "decodeMetadata" "payload length mismatch" "DataView")
    string(FIND "${ts_decoder}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "generated TypeScript decoder is missing ${required}")
    endif()
endforeach()

file(WRITE "${TEST_ROOT}/duplicate.def"
    "UESTCRADAR_CONTRACT(iq, IQFrame, IQMetadata)\n"
    "UESTCRADAR_CONTRACT(iq, IQFrame, IQMetadata)\n")
execute_process(
    COMMAND "${CODEGEN}" "${TEST_ROOT}/duplicate.def"
        "${SDK_SOURCE_DIR}/contracts" "${TEST_ROOT}/duplicate"
    RESULT_VARIABLE duplicate_result)
if(duplicate_result EQUAL 0)
    message(FATAL_ERROR "duplicate type_id was accepted")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/overlap-contracts")
file(READ "${SDK_SOURCE_DIR}/contracts/iq.json" overlap_json)
string(REPLACE
    "\"samples_per_channel\", \"type\": \"uint32\", \"offset\": 4"
    "\"samples_per_channel\", \"type\": \"uint32\", \"offset\": 0"
    overlap_json "${overlap_json}")
file(WRITE "${TEST_ROOT}/overlap-contracts/iq.json" "${overlap_json}")
file(WRITE "${TEST_ROOT}/overlap.def"
    "UESTCRADAR_CONTRACT(iq, IQFrame, IQMetadata)\n")
execute_process(
    COMMAND "${CODEGEN}" "${TEST_ROOT}/overlap.def"
        "${TEST_ROOT}/overlap-contracts" "${TEST_ROOT}/overlap"
    RESULT_VARIABLE overlap_result)
if(overlap_result EQUAL 0)
    message(FATAL_ERROR "overlapping wire fields were accepted")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/version-contracts")
foreach(contract iq pulse_compression rd)
    file(READ "${SDK_SOURCE_DIR}/contracts/${contract}.json" contract_json)
    if(contract STREQUAL "rd")
        string(REPLACE "\"type_version\": 2" "\"type_version\": 3"
            contract_json "${contract_json}")
    endif()
    file(WRITE "${TEST_ROOT}/version-contracts/${contract}.json"
        "${contract_json}")
endforeach()
execute_process(
    COMMAND "${CODEGEN}"
        "${SDK_SOURCE_DIR}/include/contract_catalog.def"
        "${TEST_ROOT}/version-contracts" "${TEST_ROOT}/version"
    RESULT_VARIABLE version_result
    ERROR_VARIABLE version_error)
if(NOT version_result EQUAL 0)
    message(FATAL_ERROR "independent version fixture failed: ${version_error}")
endif()
file(READ "${TEST_ROOT}/version/contract_iq.generated.cpp" iq_traits)
file(READ "${TEST_ROOT}/version/contract_pulse_compression.generated.cpp"
    pulse_traits)
file(READ "${TEST_ROOT}/version/contract_rd.generated.cpp" rd_traits)
if(NOT iq_traits MATCHES "type_version.*return 2U" OR
   NOT pulse_traits MATCHES "type_version.*return 2U" OR
   NOT rd_traits MATCHES "type_version.*return 3U")
    message(FATAL_ERROR "RD version change affected an unrelated contract")
endif()

file(READ "${SDK_SOURCE_DIR}/src/sdk.cpp" sdk_source)
foreach(forbidden "FrameKind" "DEFINE_PARENT_CREATE" "kContractVersion")
    string(FIND "${sdk_source}" "${forbidden}" forbidden_position)
    if(NOT forbidden_position EQUAL -1)
        message(FATAL_ERROR "generic SDK contains forbidden token ${forbidden}")
    endif()
endforeach()

file(READ "${SDK_SOURCE_DIR}/include/data.h" public_header)
foreach(forbidden
    "const IQFrame& parent"
    "const PulseCompressionFrame& parent"
    "const RDFrame& parent")
    string(FIND "${public_header}" "${forbidden}" forbidden_position)
    if(NOT forbidden_position EQUAL -1)
        message(FATAL_ERROR "public SDK still contains an N^2 parent overload")
    endif()
endforeach()
