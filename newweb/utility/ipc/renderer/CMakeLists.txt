set(RENDERER_FBS
  message_types.fbs
  load_msg.fbs
  load_resp_msg.fbs
  )

set(COMBINED_HEADERS_CONTENT "")

foreach(in_fname ${RENDERER_FBS})
  string(REPLACE ".fbs" "_generated.h" out_fname ${in_fname})
  set(COMBINED_HEADERS_CONTENT "${COMBINED_HEADERS_CONTENT}#include \"${out_fname}\"\n")
endforeach()

MESSAGE("\n${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers will include:\n${COMBINED_HEADERS_CONTENT}")

FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/.copy_combined_headers "${COMBINED_HEADERS_CONTENT}")

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers
  COMMENT "generating flatbuffers headers for transport proxy"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND flatc --cpp -o ${CMAKE_CURRENT_BINARY_DIR}/gen/ ${RENDERER_FBS}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/gen/.copy_combined_headers ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers
  DEPENDS ${RENDERER_FBS}
  VERBATIM
)

ADD_CUSTOM_TARGET(renderer_ipc_messages_flatbuffers
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers)