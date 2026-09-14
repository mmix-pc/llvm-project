# The Linux entry is preparation only until the complete CRT is composed.
add_startup_object(
  start
  SRC start.cpp
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector
)
