
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen/io_service_messages_flatbuffers_generated.h
  COMMAND flatc --cpp -o ${CMAKE_CURRENT_BINARY_DIR}/gen ${CMAKE_CURRENT_SOURCE_DIR}/io_service_messages_flatbuffers.fbs
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/io_service_messages_flatbuffers.fbs
)

ADD_CUSTOM_TARGET(io_service_ipc_messages_flatbuffers
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gen/io_service_messages_flatbuffers_generated.h)
