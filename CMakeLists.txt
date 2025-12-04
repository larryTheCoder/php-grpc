cmake_minimum_required(VERSION 3.25)
project(php_grpc C)

set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE SRC_LIST FOLLOW_SYMLINKS *.c)
file(GLOB_RECURSE HEAD_FILES FOLLOW_SYMLINKS *.h)

set(PHP_SDK_ROOT C:/pocketmine-php)
set(PHP_SOURCE ${PHP_SDK_ROOT}/php-8.4.14-release)
set(PHP_DEPENDENCIES ${PHP_SDK_ROOT}/deps-php-8.4-release)

include_directories(BEFORE ./ ./src ${PHP_SOURCE} ${PHP_DEPENDENCIES}/include ${PHP_SOURCE}/main ${PHP_SOURCE}/sapi ${PHP_SOURCE}/TSRM ${PHP_SOURCE}/Zend)

add_library(php_grpc SHARED ${SRC_LIST} ${HEAD_FILES})