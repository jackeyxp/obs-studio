set(PACKAGE_VERSION "23.2.2-rc1")

if("${PACKAGE_VERSION}" VERSION_LESS "${PACKAGE_FIND_VERSION}")
	set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
	set(PACKAGE_VERSION_COMPATIBLE TRUE)
	if ("${PACKAGE_VERSION}" VERSION_EQUAL "${PACKAGE_FIND_VERSION}")
		set(PACKAGE_VERSION_EXACT TRUE)
	endif()
endif()
