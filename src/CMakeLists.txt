set(CONVERTER_LIB "ge2i")
set(CONVERTER_BIN "ge_to_ismrmrd")

include_directories(
  ${ISMRMRD_INCLUDE_DIR}
  ${ORCHESTRA_INCLUDE_DIRS}
  ${LIBXSLT_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR})

add_library(${CONVERTER_LIB} STATIC
  GERawConverter.cpp)
    
target_link_libraries(${CONVERTER_LIB}
  ${ORCHESTRA_LIBRARIES}
  ${LIBXSLT_LIBRARIES}
  ${LIBXML2_LIBRARIES}
  dl)

# Will make static for portability
#install(TARGETS ${CONVERTER_LIB} DESTINATION lib)
install(FILES SequenceConverter.h DESTINATION include/ge-tools)

add_executable(${CONVERTER_BIN} main.cpp)
target_link_libraries(${CONVERTER_BIN}
    ${CONVERTER_LIB}
    ${ISMRMRD_LIBRARIES})
install(TARGETS ${CONVERTER_BIN} DESTINATION bin)

install(FILES config/default.xml config/default.xsl DESTINATION share/ge-tools/config)

# API documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_TARGET "doc")
    set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${DOXYGEN_TARGET}/html/index.html)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # configure Doxyfile.in, generating Doxyfile
    configure_file(${DOXYFILE_IN} ${DOXYFILE} @ONLY)

    add_custom_command(
        OUTPUT ${DOXYGEN_OUTPUT}
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API Documentation with Doxygen..." VERBATIM
        DEPENDS ${DOXYFILE})

    add_custom_target(${DOXYGEN_TARGET} DEPENDS ${DOXYGEN_OUTPUT})
else(DOXYGEN_FOUND)
    message("Doxygen is needed to build ge-ismrmrd's API docs")
endif(DOXYGEN_FOUND)
