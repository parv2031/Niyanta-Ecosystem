# Minimum required version of CMake needed to build this package.
# ROS2 typically requires at least version 3.5+, but newer versions are recommended.
cmake_minimum_required(VERSION 3.8)

# Defines the name of the project/package being built.
# This must match the package name defined in package.xml.
project(testbed_navigation)


# ---------------------------------------------------------
# Compiler Warning Settings
# ---------------------------------------------------------

# If the compiler is GCC or Clang, enable additional warning flags.
# These flags help detect potential issues during compilation.
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")

  # -Wall     : Enable most warning messages
  # -Wextra   : Enable extra warnings not included in -Wall
  # -Wpedantic: Enforce strict ISO C++ compliance
  add_compile_options(-Wall -Wextra -Wpedantic)

endif()


# ---------------------------------------------------------
# Find Required Dependencies
# ---------------------------------------------------------

# Find the core ROS2 build system package.
# ament_cmake provides macros and functions required to build ROS2 packages.
find_package(ament_cmake REQUIRED)

# Uncomment the following section in order to manually add more dependencies.
# For example, if this package used other ROS2 libraries.
# find_package(<dependency> REQUIRED)


# ---------------------------------------------------------
# Testing and Linting Configuration
# ---------------------------------------------------------

# BUILD_TESTING is a standard CMake option used when running tests.
# This block enables automatic linting and code quality checks.
if(BUILD_TESTING)

  # Automatically adds common linting tools for ROS2 packages.
  find_package(ament_lint_auto REQUIRED)

  # The following line disables the copyright checker.
  # This is useful during development before license headers are added.
  set(ament_cmake_copyright_FOUND TRUE)

  # The following line disables cpplint checks.
  # cpplint checks coding style for C++ source files.
  set(ament_cmake_cpplint_FOUND TRUE)

  # Automatically adds test dependencies required by the linting tools.
  ament_lint_auto_find_test_dependencies()

endif()


# ---------------------------------------------------------
# Install Launch Files
# ---------------------------------------------------------

# This command installs the "launch" directory into the package's
# install location so ROS2 can locate the launch files after building.
install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)


# ---------------------------------------------------------
# Install Configuration Files
# ---------------------------------------------------------

# This installs the "config" directory which contains YAML parameter files
# used by Nav2 components such as:
# - AMCL parameters
# - Navigation stack parameters
install(
  DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)


# ---------------------------------------------------------
# Declare the package to the ROS2 build system
# ---------------------------------------------------------

# ament_package() registers the package with the ROS2 build system.
# It generates metadata so that other packages can depend on this one.
ament_package()