# - Find Orchestra SDK
#
#  ORCHESTRA_INCLUDE_DIR    - top-level include dir only
#  ORCHESTRA_INCLUDE_DIRS   - top-level include dir plus 3rd party Blitz dir
#  ORCHESTRA_LIBRARIES
#  ORCHESTRA_FOUND
#
# © 2015 Joseph Naegele, 2017 Frank Ong

# Include recon libraries exported from the SDK CMake build
set(RECON_LIBRARIES_INCLUDE_FILE ${OX_INSTALL_DIRECTORY}/lib/ReconLibraries.cmake)
if(EXISTS ${RECON_LIBRARIES_INCLUDE_FILE})
    include (${RECON_LIBRARIES_INCLUDE_FILE})
else()
    message("Could not find ${RECON_LIBRARIES_INCLUDE_FILE}")
    message(FATAL_ERROR "Verify that the CMake OX_INSTALL_DIRECTORY option is set correctly")
endif()

# Include SDK build configuration
set(TOPDIR "${OX_INSTALL_DIRECTORY}/include")
include (${TOPDIR}/recon/SDK/product.cmake)

list(APPEND ORCHESTRA_LIBRARIES Cartesian2D
				Cartesian3D
	    			Gradwarp				
				Legacy
				Core
				CalibrationCommon
				Control
				Common
				Crucial
				Dicom
				Hdf5
				Math
				System
				${OX_3P_LIBS}
				${OX_OS_LIBS}
				)
