set(IO_SERVICE_FBS
  message_types.fbs.txt
  request_resource_msg.fbs.txt
  received_response_msg.fbs.txt
  data_received_msg.fbs.txt
  request_complete_msg.fbs.txt
  reset_session_msg.fbs.txt
  )

set(COMBINED_HEADERS_CONTENT "")

foreach(in_fname ${IO_SERVICE_FBS})
  string(REPLACE ".fbs.txt" ".fbs_generated.h" out_fname ${in_fname})
  set(COMBINED_HEADERS_CONTENT "${COMBINED_HEADERS_CONTENT}#include \"${out_fname}\"\n")
endforeach()

MESSAGE("\n${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers will include:\n${COMBINED_HEADERS_CONTENT}")

FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/.copy_combined_headers "${COMBINED_HEADERS_CONTENT}")

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers
  COMMENT "generating flatbuffers headers for io service"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND flatc --cpp -o ${CMAKE_CURRENT_BINARY_DIR}/gen/ ${IO_SERVICE_FBS}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/gen/.copy_combined_headers ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers
  DEPENDS ${IO_SERVICE_FBS}
  VERBATIM
)

ADD_CUSTOM_TARGET(io_service_ipc_messages_flatbuffers
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers)
