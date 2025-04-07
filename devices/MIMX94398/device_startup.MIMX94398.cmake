# Add set(CONFIG_USE_device_startup true) in config.cmake to use this component

include_guard(GLOBAL)
message("${CMAKE_CURRENT_LIST_FILE} component is included.")

      if(CONFIG_TOOLCHAIN STREQUAL iar AND CONFIG_CORE_ID STREQUAL cm33_core0)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/iar/startup_MIMX94398_cm33_core0.s "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL armgcc AND CONFIG_CORE_ID STREQUAL cm33_core0)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/gcc/startup_MIMX94398_cm33_core0.S "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL iar AND CONFIG_CORE_ID STREQUAL cm33_core1)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/iar/startup_MIMX94398_cm33_core1.s "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL armgcc AND CONFIG_CORE_ID STREQUAL cm33_core1)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/gcc/startup_MIMX94398_cm33_core1.S "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL iar AND CONFIG_CORE_ID STREQUAL cm7_core0)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/iar/startup_MIMX94398_cm7_core0.s "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL armgcc AND CONFIG_CORE_ID STREQUAL cm7_core0)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/gcc/startup_MIMX94398_cm7_core0.S "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL iar AND CONFIG_CORE_ID STREQUAL cm7_core1)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/iar/startup_MIMX94398_cm7_core1.s "" device_startup.MIMX94398)
        endif()

        if(CONFIG_TOOLCHAIN STREQUAL armgcc AND CONFIG_CORE_ID STREQUAL cm7_core1)
          add_config_file(${CMAKE_CURRENT_LIST_DIR}/gcc/startup_MIMX94398_cm7_core1.S "" device_startup.MIMX94398)
        endif()

  
  if(CONFIG_USE_COMPONENT_CONFIGURATION)
  message("===>Import configuration from ${CMAKE_CURRENT_LIST_FILE}")

  
            if(CONFIG_TOOLCHAIN STREQUAL armgcc)
      target_compile_options(${MCUX_SDK_PROJECT_NAME} PUBLIC
            )
      endif()
      
  endif()

