cmake_minimum_required(VERSION 3.20)

if(CONFIG_ZMK_BATTERY_REPORT_BEHAVIOR)
  target_sources(app PRIVATE src/behaviors/behavior_battery_report.c)
endif()
