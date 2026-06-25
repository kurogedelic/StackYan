# stackchu_hal コンポーネント定義
idf_component_register(
    SRCS
        "src/Hal.cpp"
        "src/RgbSk6812.cpp"
        "src/PowerAxp2101.cpp"
        "src/MotionBmi270.cpp"
        "src/BacklightAxp2101.cpp"
        "src/IrNec.cpp"
        "src/ServoFtBus.cpp"
        "src/StorageFatFs.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_timer
        fatfs
        wear_levelling
)

# C++17 を明示
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
