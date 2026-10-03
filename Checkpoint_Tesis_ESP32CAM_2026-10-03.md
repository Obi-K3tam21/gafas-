# CHECKPOINT — Tesis de grado: Gafas inteligentes con ESP32-CAM + ESP8266
Fecha: 2026-10-03

## Objetivo general
Desarrollar un sistema de gafas inteligentes de asistencia visual de bajo costo, orientado principalmente a personas con baja visión y/o limitaciones motrices. El prototipo utilizará visión embebida para reconocer objetos, leer códigos QR y ejecutar acciones mediante dos microcontroladores.

## Arquitectura actual

### ESP32-CAM AI Thinker
Rol: percepción y decisión visual.

Hardware:
- ESP32-D0WD-V3
- 2 núcleos
- 4 MB Flash
- 4 MB PSRAM
- OV2640
- AI Thinker

Funciones:
- Captura de imágenes.
- Lectura QR.
- Inferencia Edge Impulse.
- Decisión visual.
- Envío de comandos al ESP8266.

Configuración de cámara actual:
- QVGA 320x240.
- JPEG.
- PSRAM habilitada.
- 2 frame buffers.
- CAMERA_GRAB_LATEST.

### ESP8266 NodeMCU
Rol: controlador de acciones y salidas.

Funciones previstas:
- Recibir comandos desde ESP32-CAM.
- Activar/desactivar relé.
- Generar audios de los objetos reconocidos.
- Confirmar acciones realizadas.

Comunicación prevista:
- Wi-Fi / TCP.

## Módulo QR
Librería actual:
- ESP32QRCodeReader.

Estado:
- Lectura QR funcional.
- Confirmación de 3 lecturas consecutivas.
- Lógica ON/OFF implementada conceptualmente.
- Cooldown de 30 s para evitar repetición del mismo comando.
- Si durante el cooldown aparece el estado contrario, este puede ejecutarse inmediatamente y desbloquear el anterior.

Ejemplo:
ON -> confirmar 3 veces -> ejecutar -> bloquear ON 30 s.
OFF durante esos 30 s -> confirmar -> ejecutar -> ON vuelve a quedar disponible.

## Orientación física de la cámara
La ESP32-CAM ya está instalada en su orientación física final.

La imagen real sale rotada aproximadamente 90° respecto a una vista convencional.

Decisión:
- NO rotar los frames reales por software para entrenamiento ni inferencia.
- Capturar el dataset en la orientación física final.
- Entrenar Edge Impulse con esa orientación.
- Corregir únicamente la vista previa del WebServer mediante CSS/JavaScript para comodidad del usuario.
- Los JPEG descargados permanecen en la orientación real.

Para QR:
- La rotación física no debería impedir el reconocimiento por sí sola.
- Factores críticos: nitidez, tamaño, contraste, iluminación, reflejos, movimiento y perspectiva.

## WebServer de dataset
WebServer embebido directamente en el .ino del ESP32-CAM.

Funciones actuales:
- Vista en vivo.
- Rotación visual del preview:
  - 90° izquierda.
  - 90° derecha.
  - sin rotación.
- Captura manual.
- Captura automática con intervalo configurable.
- Nombre base de archivos.
- Galería.
- Selección de imágenes.
- Limpieza.
- Descarga de seleccionadas en un ZIP.
- Descarga de todas en un ZIP.

Importante:
- La rotación del preview es solo visual.
- Las fotografías reales no se rotan.

## Edge Impulse
Objetivo final:
Reconocer cinco objetos comunes de un área de trabajo.

Clases propuestas:
- taza
- botella
- celular
- mouse
- teclado
- background como clase de rechazo.

### Fase piloto actual
Trabajar únicamente con:
- taza
- background

Dataset inicial:
- 50 imágenes de taza.
- 50 imágenes de background.

Luego aumentar progresivamente:
- 100 + 100
- 150 + 150
- 200 + 200

## Reglas de captura
Las imágenes deben tomarse:
- Con la ESP32-CAM ya montada en su posición final.
- Variando distancia.
- Variando posición del objeto.
- Variando ángulo.
- Variando fondo.
- Variando iluminación.
- Con varios ejemplares si es posible.

### Background
No necesita ser un escritorio vacío.
Debe representar escenas reales sin una taza claramente visible.

Ejemplos:
- herramientas,
- cables,
- documentos,
- paredes,
- piso,
- silla,
- cajas,
- equipos,
- manos,
- objetos varios.

## Iluminación
Distribución inicial sugerida:
- 50 % iluminación normal.
- 25 % más clara.
- 25 % ligeramente baja.

Evitar:
- oscuridad excesiva,
- pérdida de contornos,
- reflejos fuertes,
- contraluz severo,
- sobreexposición.

## Configuración Edge Impulse deseada
Para el piloto:
- Image Classification.
- 96 x 96.
- Grayscale.
- 2 clases:
  - taza
  - background

Evitar:
- FOMO.
- SSD.
- FPN.
- Object Detection.
- Bounding boxes.

## Prueba anterior descartada
Se entrenó accidentalmente un modelo de Object Detection.

Resultados aproximados:
- Accuracy mostrada: 41.86 %
- Precision no-background: ~0.70
- Recall: ~0.47
- F1: ~0.56

No se consideran resultados finales porque el problema estaba planteado como detección de objetos, cuando la fase actual requiere clasificación binaria.

## Integración futura QR + IA
No se pretende ejecutar ambos procesos pesados compitiendo continuamente por la cámara.

Propuesta:
QR -> QR -> IA -> QR -> QR -> IA

La frecuencia definitiva se decidirá después de medir:
- tiempo de inferencia,
- RAM,
- PSRAM,
- flash,
- estabilidad.

## Estado del proyecto
- ESP32-CAM estable: COMPLETADO
- WebServer funcional: COMPLETADO
- Captura de dataset: COMPLETADO
- Rotación visual del WebServer: COMPLETADO
- QR local: COMPLETADO
- Confirmación QR 3/3: COMPLETADO
- Seguridad ON/OFF + cooldown: COMPLETADO
- Dataset piloto taza/background: EN PROCESO
- Image Classification Edge Impulse: EN PROCESO
- Inferencia local ESP32-CAM: PENDIENTE
- Integración QR + IA: PENDIENTE
- Comunicación final ESP32-CAM -> ESP8266: PENDIENTE
- Audio de objetos mediante ESP8266: PENDIENTE
- Control final del relé: PENDIENTE

## Próximo objetivo
1. Capturar 50 imágenes de TAZA y 50 de BACKGROUND con la cámara en su posición final.
2. Crear correctamente el Impulse de Image Classification.
3. Entrenar y evaluar el modelo.
4. Exportar la librería Arduino.
5. Integrar inferencia al ESP32-CAM.
6. Medir consumo de recursos y tiempo.
7. Integrar QR + IA.
8. Conectar ESP8266 para audio y relé.

## Regla de continuidad
No cambiar componentes que ya funcionan salvo que sea estrictamente necesario. Mantener la arquitectura:
ESP32-CAM = percepción visual.
ESP8266 = acciones, audio y relé.
