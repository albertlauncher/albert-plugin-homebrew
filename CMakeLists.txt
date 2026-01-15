cmake_minimum_required(VERSION 3.16)
project(homebrew VERSION 0.0.0)

find_package(Albert REQUIRED)
find_package(QCoro6 REQUIRED COMPONENTS Coro)

albert_plugin(
    QT
        Core
    INCLUDE PRIVATE
        $<TARGET_PROPERTY:albert::applications,INTERFACE_INCLUDE_DIRECTORIES>
    LINK PRIVATE
        QCoro6::Coro

)
