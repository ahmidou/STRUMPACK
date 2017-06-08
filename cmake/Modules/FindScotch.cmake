if (NOT (SCOTCH_INCLUDES AND SCOTCH_LIBRARIES))
  find_path(SCOTCH_INCLUDES NAMES scotch.h PATHS $ENV{SCOTCHDIR} PATH_SUFFIXES include)
  find_library(SCOTCH_LIBRARY       scotch      PATHS /usr/lib64/mpich/lib ENV SCOTCHDIR PATH_SUFFIXES lib)
  find_library(PTSCOTCH_LIBRARY     ptscotch    PATHS /usr/lib64/mpich/lib ENV SCOTCHDIR PATH_SUFFIXES lib)
  find_library(SCOTCHERR_LIBRARY    scotcherr   PATHS /usr/lib64/mpich/lib ENV SCOTCHDIR PATH_SUFFIXES lib)
  find_library(PTSCOTCHERR_LIBRARY  ptscotcherr PATHS /usr/lib64/mpich/lib ENV SCOTCHDIR PATH_SUFFIXES lib)
  set(SCOTCH_LIBRARIES ${SCOTCH_LIBRARY} ${PTSCOTCH_LIBRARY} ${SCOTCHERR_LIBRARY} ${PTSCOTCHERR_LIBRARY})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SCOTCH DEFAULT_MSG SCOTCH_INCLUDES SCOTCH_LIBRARIES)