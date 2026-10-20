	# Minimum CMake version required, we'll just use the latest version.
    cmake_minimum_required(VERSION 3.22)
    # Project name, version and description
    project(discord-bot VERSION 1.0 DESCRIPTION "A discord bot")

    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

    # Create an executable
    add_executable(${PROJECT_NAME}
        src/main.cpp
        src/helper.cpp
        src/pdf_extract.cpp
    )

    # Find our pre-installed DPP package (using FindDPP.cmake).
    find_package(DPP REQUIRED)
    find_package(CURL 7.85 REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)

    # Link the pre-installed DPP package.
    target_link_libraries(${PROJECT_NAME} 
        ${DPP_LIBRARIES}
    )

    # Include the DPP directories.
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${DPP_INCLUDE_DIR}
    )
    
    # Link CURL
    target_link_libraries(${PROJECT_NAME} ${CURL_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})

    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
    
    target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)
    

    # Set C++ version
    set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
