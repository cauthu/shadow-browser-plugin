set(TRANSPORT_PROXY_FBS
  message_types.fbs.txt
  establish_tunnel_msg.fbs.txt
  establish_tunnel_resp_msg.fbs.txt
  set_auto_start_defense_on_next_send_msg.fbs.txt
  set_auto_start_defense_on_next_send_resp_msg.fbs.txt
  stop_defense_msg.fbs.txt
  stop_defense_resp_msg.fbs.txt

  tunnel_closed_msg.fbs.txt
  )

set(COMBINED_HEADERS_CONTENT "")

foreach(in_fname ${TRANSPORT_PROXY_FBS})
  string(REPLACE ".fbs.txt" ".fbs_generated.h" out_fname ${in_fname})
  set(COMBINED_HEADERS_CONTENT "${COMBINED_HEADERS_CONTENT}#include \"${out_fname}\"\n")
endforeach()

MESSAGE("\n${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers will include:\n${COMBINED_HEADERS_CONTENT}")

FILE(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/.copy_combined_headers "${COMBINED_HEADERS_CONTENT}")

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers
  COMMENT "generating flatbuffers headers for transport proxy"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND flatc --cpp -o ${CMAKE_CURRENT_BINARY_DIR}/gen/ ${TRANSPORT_PROXY_FBS}
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/gen/.copy_combined_headers ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers
  DEPENDS ${TRANSPORT_PROXY_FBS}
  VERBATIM
)

ADD_CUSTOM_TARGET(transport_proxy_ipc_messages_flatbuffers
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gen/combined_headers)
