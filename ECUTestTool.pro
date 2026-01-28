QT       += core gui serialport widgets network

TARGET = ECUTestTool
TEMPLATE = app

# 针对 Win7 + 低配置优化编译参数
CONFIG += c++11 release
CONFIG += console
DEFINES += QT_DEPRECATED_WARNINGS

# 源文件列表（请确保您的文件名和这里一致）
SOURCES += \
    DeviceChannelWidget.cpp \
    MainWindow.cpp \
    PlcController.cpp \
    SnManager.cpp \
    main.cpp

# 头文件列表
HEADERS += \
    ConfigManager.h \
    DeviceChannelWidget.h \
    MainWindow.h \
    PlcController.h \
    SnManager.h



# 部署文件（让 Qt Creator 知道这个文件的存在）
DISTFILES += config.json \
    config.json

# 默认包含路径
INCLUDEPATH += $$PWD

# 尝试解决 max_align_t 重定义冲突
DEFINES += __stddef_h_builtins
