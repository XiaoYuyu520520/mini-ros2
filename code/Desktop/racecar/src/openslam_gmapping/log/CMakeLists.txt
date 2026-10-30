add_library(log configuration.cpp sensorlog.cpp sensorstream.cpp carmenconfiguration.cpp)
target_link_libraries(log sensor_base sensor_odometry sensor_range)

install(TARGETS log DESTINATION lib)

ament_export_libraries(log)
