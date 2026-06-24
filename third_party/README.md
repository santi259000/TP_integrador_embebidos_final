# Dependencias del firmware

Este directorio contiene todas las dependencias necesarias para compilar el firmware sin apoyarse en otros repositorios del workspace.

Estructura esperada:

- `libopencm3/` -> copia local incluida en este repositorio
- `FreeRTOS-Kernel/` -> copia local del kernel de FreeRTOS usada por la plantilla
- `common/linker.ld` -> linker script compartido para STM32F103C8T6

Este repo quedó preparado para que los alumnos lo clonen sin depender de submódulos extra.
El `Makefile` del firmware falla de manera explicita si alguna de estas dependencias no esta presente.
