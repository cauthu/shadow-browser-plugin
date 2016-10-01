set(CMAKE_VERBOSE_MAKEFILE on)

include_directories(AFTER ${SPDYLAY_INCLUDES} ${OPENSSL_INCLUDES} ${EVENT2_INCLUDES})

include_directories(AFTER ${UTILITY_DIR})
include_directories(AFTER ${UTILITY_DIR}/angelscript/include)

# we want single-threaded angelscript
add_definitions(-DAS_NO_THREADS)
# add_definitions(-DAS_USE_NAMESPACE)


SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(RENDER_PROCESS_SOURCES
  main.cpp
  ipc_io_service.cpp
  ipc_renderer.cpp
  ${UTILITY_DIR}/object.cpp
  ${UTILITY_DIR}/common.cc
  ${UTILITY_DIR}/stream_channel.cpp
  ${UTILITY_DIR}/timer.cpp
  ${UTILITY_DIR}/tcp_channel.cpp
  ${UTILITY_DIR}/tcp_server.cpp
  ${UTILITY_DIR}/generic_message_channel.cpp
  ${UTILITY_DIR}/ipc/generic_ipc_channel.cpp
)

# webengine sources
set(RENDER_PROCESS_SOURCES "${RENDER_PROCESS_SOURCES}"
  webengine/webengine.cpp
  webengine/page_model.cpp
  webengine/events/EventTypeNames.cpp
  webengine/events/EventTarget.cpp
  webengine/fetch/Resource.cpp
  webengine/fetch/CSSStyleSheetResource.cpp
  webengine/fetch/ResourceFetcher.cpp
  webengine/dom/Node.cpp
  webengine/dom/Document.cpp
  webengine/dom/Element.cpp
  webengine/html/HTMLImageElement.cpp
  webengine/html/HTMLScriptElement.cpp
  webengine/html/parser/HTMLDocumentParser.cpp
  )

set(RENDER_PROCESS_SOURCES "${RENDER_PROCESS_SOURCES}"
  ${UTILITY_DIR}/angelscript/source/as_atomic.cpp
  ${UTILITY_DIR}/angelscript/source/as_builder.cpp
  ${UTILITY_DIR}/angelscript/source/as_bytecode.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_arm.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_mips.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_ppc_64.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_ppc.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_sh4.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_x64_gcc.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_x64_mingw.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_x64_msvc.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_x86.cpp
  ${UTILITY_DIR}/angelscript/source/as_callfunc_xenon.cpp
  ${UTILITY_DIR}/angelscript/source/as_compiler.cpp
  ${UTILITY_DIR}/angelscript/source/as_configgroup.cpp
  ${UTILITY_DIR}/angelscript/source/as_context.cpp
  ${UTILITY_DIR}/angelscript/source/as_datatype.cpp
  ${UTILITY_DIR}/angelscript/source/as_gc.cpp
  ${UTILITY_DIR}/angelscript/source/as_generic.cpp
  ${UTILITY_DIR}/angelscript/source/as_globalproperty.cpp
  ${UTILITY_DIR}/angelscript/source/as_memory.cpp
  ${UTILITY_DIR}/angelscript/source/as_module.cpp
  ${UTILITY_DIR}/angelscript/source/as_objecttype.cpp
  ${UTILITY_DIR}/angelscript/source/as_outputbuffer.cpp
  ${UTILITY_DIR}/angelscript/source/as_parser.cpp
  ${UTILITY_DIR}/angelscript/source/as_restore.cpp
  ${UTILITY_DIR}/angelscript/source/as_scriptcode.cpp
  ${UTILITY_DIR}/angelscript/source/as_scriptengine.cpp
  ${UTILITY_DIR}/angelscript/source/as_scriptfunction.cpp
  ${UTILITY_DIR}/angelscript/source/as_scriptnode.cpp
  ${UTILITY_DIR}/angelscript/source/as_scriptobject.cpp
  ${UTILITY_DIR}/angelscript/source/as_string.cpp
  ${UTILITY_DIR}/angelscript/source/as_string_util.cpp
  ${UTILITY_DIR}/angelscript/source/as_thread.cpp
  ${UTILITY_DIR}/angelscript/source/as_tokenizer.cpp
  ${UTILITY_DIR}/angelscript/source/as_typeinfo.cpp
  ${UTILITY_DIR}/angelscript/source/as_variablescope.cpp
)

set(RENDER_PROCESS_SOURCES "${RENDER_PROCESS_SOURCES}"
  ${UTILITY_DIR}/angelscript/add_on/scriptstdstring/scriptstdstring.cpp
  ${UTILITY_DIR}/angelscript/add_on/scriptstdstring/scriptstdstring_utils.cpp

  ${UTILITY_DIR}/angelscript/add_on/scriptarray/scriptarray.cpp

)

set(LINK_LIBS ${EVENT2_LIBRARIES})

set(IPC_MSG_BUFS
  io_service_ipc_messages_flatbuffers
  renderer_ipc_messages_flatbuffers
)

## create and install a shared library that can plug into shadow
add_definitions(-DIN_SHADOW)
add_shadow_plugin(shadow-plugin-render_process ${RENDER_PROCESS_SOURCES})
target_link_libraries(shadow-plugin-render_process ${LINK_LIBS})
install(TARGETS shadow-plugin-render_process DESTINATION plugins)

add_dependencies(
  shadow-plugin-render_process-bitcode
  ${IPC_MSG_BUFS}
)

## create and install an executable that can run outside of shadow
remove_definitions(-DIN_SHADOW)
add_executable(render_process ${RENDER_PROCESS_SOURCES})
target_link_libraries(render_process ${LINK_LIBS})
install(TARGETS render_process DESTINATION bin)

add_dependencies(
  render_process
  ${IPC_MSG_BUFS}
)
