# Space Flight

[Русский](README.md) · [English](README.en.md) · **Español**

Un pequeño juego 2D tranquilo sobre volar entre estrellas y asteroides. Los láseres extraen mineral, un árbol de mejoras desarrolla la nave y la exploración desbloquea sectores, expediciones y encuentros poco frecuentes. Puedes pilotar manualmente o activar el piloto automático. Creado **just for fun**.

El juego está disponible en ruso, inglés y español. Pulsa **L** para cambiar de idioma; la tecla aparece en pantalla. La elección se guarda por separado del progreso.

## Controles

| Teclas | Acción |
|---|---|
| A / D o ← / → | Vuelo manual |
| F | Piloto automático |
| Tab | Árbol de mejoras |
| Ratón | Comprar una mejora; arrastrar el fondo o usar el botón central / derecho; rueda para ampliar |
| G / T / B / R / J | Siguiente mejora / seguimiento / autocompra / centrar árbol / diario |
| X | Prioridad de objetivos del láser |
| C | Cambiar de sector desbloqueado |
| E | Expediciones y recompensas |
| I / M / K | Refinado / fabricador / especialización; Enter confirma y Esc cancela |
| H | Interfaz detallada y ayuda |
| L | Русский / English / Español |
| − / + | Brillo de las estrellas |
| Esc | Pausa o cerrar panel |
| N y otra vez N en pausa | Iniciar un vuelo nuevo y reiniciar el progreso |
| P | Prueba de carga sin guardar el progreso |
| Q | Salir |

## Ejecutar y compilar

Se necesitan Unreal Engine 5.8 y las herramientas de C++ compatibles con Windows. Abre `SpaceFlight.uproject` en Unreal Editor y ejecuta el juego; también puedes indicar la ruta completa de `UnrealEditor.exe` en la variable `UE_EDITOR` y ejecutar `Play.bat`. Los recursos de Unreal están en `Content` y las imágenes y sonidos originales en `Art`.

Las partidas y los ajustes locales permanecen en tu ordenador y se excluyen del repositorio. Las pruebas de C++ están en `Source/SpaceFlight`; las pruebas de audio requieren Python y NumPy.

Licencia del proyecto: [MIT](LICENSE). Unreal Engine está sujeto a los términos independientes de Epic Games.
