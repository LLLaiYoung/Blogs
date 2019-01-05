add_llvm_library(QTPlugin MODULE QTPlugin.cpp PLUGIN_TOOL clang)

if(LLVM_ENABLE_PLUGINS AND (WIN32 OR CYGWIN))
  target_link_libraries(QTPlugin PRIVATE
    clangAST
    clangBasic
    clangFrontend
    LLVMSupport
    )
endif()
