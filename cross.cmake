set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")

set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")

function(add_assets target)
  set(result)
  foreach(in_f ${ARGN})
    set(asm_f "${CMAKE_CURRENT_BINARY_DIR}/${in_f}.S")
    set(out_f "${CMAKE_CURRENT_BINARY_DIR}/${in_f}.o")
    string(REPLACE "/" "_" symbol ${in_f})
    string(REPLACE "." "_" symbol ${symbol})
    get_filename_component(out_dir ${out_f} DIRECTORY)
    # we use this embedding method to enforce alignment on resources
    # which is needed for shaders
    file(WRITE ${asm_f}
".section .rodata
.global _binary_${symbol}_start
.global _binary_${symbol}_end
.align  4
_binary_${symbol}_start:
.incbin \"${CMAKE_CURRENT_SOURCE_DIR}/${in_f}\"
_binary_${symbol}_end:"
    )
    add_custom_command(OUTPUT ${out_f}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
      COMMAND ${CMAKE_ASM_COMPILER} -c -o ${out_f} ${asm_f}
      DEPENDS ${in_f} ${asm_f}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Using ${in_f}"
      VERBATIM
    )
    list(APPEND result ${out_f})
  endforeach()
  set(${target} "${result}" PARENT_SCOPE)
endfunction()

add_assets(assets
  assets/background.png
  assets/imgui_v_cg.gxp
  assets/imgui_f_cg.gxp
)

add_executable(pkgj
  ${assets}
  src/aes128.cpp
  src/bgdl.cpp
  src/comppackdb.cpp
  src/config.cpp
  src/db.cpp
  src/dialog.cpp
  src/download.cpp
  src/downloader.cpp
  src/extractzip.cpp
  src/filedownload.cpp
  src/gameview.cpp
  src/patchinfo.cpp
  src/patchinfofetcher.cpp
  src/imagefetcher.cpp
  src/imgui.cpp
  src/install.cpp
  src/menu.cpp
  src/pkgi.cpp
  src/puff.c
  src/sfo.cpp
  src/sha256.cpp
  src/update.cpp
  src/vita.cpp
  src/vitafile.cpp
  src/vitahttp.cpp
  src/zrif.cpp
)

target_link_libraries(pkgj
  vita2d
  CONAN_PKG::fmt
  CONAN_PKG::boost_scope_exit
  CONAN_PKG::vitasqlite
  CONAN_PKG::cereal
  CONAN_PKG::libzip
  CONAN_PKG::imgui
  CONAN_PKG::taihen
  png
  jpeg
  z
  m
  SceAppMgr_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  ScePgf_stub
  ScePower_stub
  ScePromoterUtil_stub
  SceShellSvc_stub
  SceSsl_stub
  SceSysmodule_stub
  SceVshBridge_stub
)

set_target_properties(pkgj PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_CURRENT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}
)

vita_create_self(eboot.bin pkgj UNSAFE)

configure_file(
   assets/sce_sys/livearea/contents/template.xml.in
   assets/sce_sys/livearea/contents/template.xml
)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION 0${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/sce_sys/icon0.png sce_sys/icon0.png
       assets/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       assets/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       ${CMAKE_CURRENT_BINARY_DIR}/assets/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

add_custom_target(send
  COMMAND curl -T eboot.bin ftp://"$ENV{PSVITAIP}":1337/ux0:/app/${VITA_TITLEID}/
  DEPENDS eboot.bin
)

add_custom_target(copy
  COMMAND cp eboot.bin ${PSVITADRIVE}/app/${VITA_TITLEID}/eboot.bin
  DEPENDS eboot.bin
)
