# ============================================================================
# Unit tests (optional)
# ============================================================================
# 默认不构建。开启方式：
#   cmake -S . -B build -DBUILD_TESTING=ON
#   cmake --build build --target chapter-utils-tests
#   ctest --test-dir build --output-on-failure
#
# 依赖 Qt6::Test，不依赖 libobs，和主插件分离。
# ============================================================================

include(CTest)

if(BUILD_TESTING)
	message(STATUS "Building chapter-utils unit tests")

	find_qt(COMPONENTS Core Test)

	add_executable(chapter-utils-tests test_chapter_utils.cpp)
	target_link_libraries(chapter-utils-tests PRIVATE Qt::Core Qt::Test)
	set_target_properties(chapter-utils-tests PROPERTIES AUTOMOC ON)
	target_include_directories(chapter-utils-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

	add_test(NAME chapter-utils COMMAND chapter-utils-tests)
endif()
