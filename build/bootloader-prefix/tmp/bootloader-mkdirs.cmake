# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Espressiff/Espressif/frameworks/esp-idf-v5.2.2/components/bootloader/subproject"
  "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader"
  "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix"
  "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix/tmp"
  "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix/src/bootloader-stamp"
  "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix/src"
  "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/EMY/ESP_workspace/firebase_deneme/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
