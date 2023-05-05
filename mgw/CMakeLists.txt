#需要编译的文件
file(GLOB MGW_SRC_LIST core/*.cpp core/*.cc core/*.h core/*.hpp
  core/Dmsp/*.cpp core/Dmsp/*.cc core/Dmsp/*.h core/Dmsp/*.hpp
  message/*.cpp message/*.cc message/*.h message/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/install/include/*.h)
#添加生成的目标->静态库文件libmgw.a
add_library(mgw STATIC ${MGW_SRC_LIST})
#设置别名ZLMediaKit::MGW
add_library(ZLMediaKit::MGW ALIAS mgw)
#指定需要链接的库
target_link_libraries(mgw
  PUBLIC
    ZLMediaKit::MediaKit
    ZLMediaKit::ToolKit)
#指定编译选项，继承自工程根目录设置的编译选项
target_compile_options(mgw
  PRIVATE ${COMPILE_OPTIONS_DEFAULT})
#指定头文件路径
target_include_directories(mgw
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/install/include>"
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../src>")

update_cached_list(MK_COMPILE_DEFINITIONS ENABLE_MGW)
update_cached_list(MK_LINK_LIBRARIES ZLMediaKit::MGW)

message(STATUS "mgw 业务功能已开启")