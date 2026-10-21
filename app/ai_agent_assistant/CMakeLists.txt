# 6-inch e-paper todo application
if(CONFIG_LVX_USE_DEMO_CONTEST2026_191_EPAPER_TODO)
  nuttx_add_application(
    NAME epaper_todo
    SRCS epaper_todo_main.c
    STACKSIZE 4096)
endif()
