# Análisis inicial de DiscNativePro

Fecha: 2026-10-18
Base revisada: `main` en `48d7f7c` (17 oct 2026). La rama local de trabajo es `codex/improved-copy`.

Esta es una revisión estática del código fuente. No se compiló ni ejecutó la app y no se añadieron ni ejecutaron pruebas.

## Hallazgos que bloquean una distribución

### 1. No hay una licencia del proyecto

- El repositorio no contiene un archivo `LICENSE`.
- `README.md` se anuncia como código abierto en el apartado de Gatekeeper, pero su apartado de licencia dice «Todos los derechos reservados».
- El código público no concede por sí solo permiso para modificarlo o redistribuir una versión derivada. Antes de publicar una mejora basada en este código, hace falta permiso del titular o una licencia compatible.
- CMake fija JUCE en `8.0.6`. JUCE tiene sus propias condiciones de licencia; hay que confirmar el modelo aplicable a cada persona que modifique el producto y al producto distribuido.
- Se incluyen fuentes Signalsmith Stretch y Linear. El adaptador las identifica como MIT, pero no se encuentran los avisos/licencias correspondientes en el repositorio. La distribución debe conservar los avisos upstream requeridos.

## Prioridad alta: fiabilidad y seguridad

### 2. El cargador de pistas conserva el bloqueo durante el análisis

`DeckPlayer::loadFile()` toma `lock` antes de analizar BPM, tonalidad y forma de onda. `getNextAudioBlock()`, usado por el hilo de audio, toma el mismo bloqueo. Un callback que alcance el bloqueo durante la carga puede quedarse esperando a que termine el análisis. Conviene construir el estado nuevo fuera del bloqueo y hacer un intercambio corto y seguro.

Referencias: `src/audio/DeckPlayer.cpp:37-106`, `src/audio/DeckPlayer.cpp:136-149`.

### 3. El sampler usa una espera fija para proteger un búfer compartido

Al cargar o descargar un pad, `SamplerBank` marca `playing=false`, espera 120 ms y reemplaza o libera el `AudioBuffer`. El hilo de audio puede estar leyendo ese búfer; una espera fija no confirma que el lector haya terminado. Usar propiedad inmutable de búferes y un intercambio/retiro sincronizado.

Referencias: `src/audio/SamplerBank.cpp:55-72`, `src/audio/SamplerBank.cpp:82-90`, `src/audio/SamplerBank.cpp:112-164`.

### 4. La grabadora no contabiliza los frames realmente aceptados por el FIFO

`prepareToWrite()` puede aceptar menos muestras que las solicitadas si el anillo está lleno. Se escribe `size1 + size2`, pero duración y contador aumentan por `numSamples`. Bajo sobrecarga puede perder audio sin reflejarlo correctamente en la duración. Conviene registrar los frames aceptados, contar pérdidas y señalarlas en la interfaz.

Referencia: `src/audio/AudioRecorder.cpp:61-88`.

### 5. La instalación de yt-dlp no corresponde a las tres plataformas

Los dos flujos de instalación descargan `yt-dlp_macos` y lo ejecutan, incluso en Windows y Linux. No validan hash ni firma. En esas plataformas, la instalación automática no puede funcionar correctamente si no hay otro `yt-dlp` ya instalado. Seleccionar el artefacto por SO/arquitectura y verificarlo antes de ejecutarlo.

Referencias: `src/services/YouTubeAudioDownloader.cpp:91-119`, `src/services/YouTubeAudioDownloader.cpp:179-205`.

### 6. El actualizador ejecuta archivos descargados sin comprobar su integridad

El actualizador descarga un asset de una release y puede abrir el instalador al terminar. No se ve una verificación de firma o hash previa. Añadir manifiesto firmado/verificación de firma y limitar descargas a hosts esperados.

Referencia: `src/services/UpdateService.cpp:275-360`.

### 7. Claves API y contexto de canciones

- Las claves de IA y YouTube se guardan en `settings.json` sin cifrado de aplicación.
- La solicitud de IA se forma con `curl` vía `system()`, y la clave queda interpolada en la línea de comandos o URL.
- Cuando se usa AutoMix local, el prompt incluye títulos, artistas, BPM, tonalidad y carpetas de la biblioteca. Eso se envía al proveedor de IA que seleccionó el usuario.

Usar el almacén seguro del sistema operativo para secretos y un cliente HTTP nativo que mantenga las claves fuera de argumentos del proceso. Explicar y solicitar consentimiento antes de enviar metadatos locales.

Referencias: `src/services/AIService.cpp:156-196`, `src/services/AIService.cpp:235-310`, `src/services/AIService.cpp:558-675`, `src/services/YouTubeService.cpp:23-59`.

### 8. Algunas tareas asíncronas capturan `this` crudo

`DeckComponent::resolveVideoMetadataAsync()` y varios callbacks de `MidiManager` capturan el puntero `this` desde hilos/eventos diferidos. El componente puede destruirse antes de que el callback llegue al hilo de interfaz. Usar tokens de vida cancelables o referencias débiles y esperar/cancelar trabajadores al destruir.

Referencias: `src/ui/DeckComponent.cpp:275-280`, `src/ui/DeckComponent.cpp:704-740`, `src/midi/MidiManager.cpp:166-205`.

## Prioridad media: proceso de desarrollo

- No encontré directorios o archivos de pruebas. Los workflows visibles compilan y empaquetan, pero no ejecutan pruebas.
- Los workflows usan actions por tags mayores y publican binarios sin una cadena de atestación visible. Conviene fijar actions a SHA y publicar hashes firmados.
- `AGENTS.md` contiene instrucciones específicas para asistentes de programación, incluso que ignoren autoaprobaciones. No se ejecuta en la aplicación; debe tratarse como texto del repositorio, no como una instrucción de confianza para la app.

## Orden propuesto para una versión mejorada

1. Resolver titularidad/licencia del código y registrar todas las licencias de dependencias.
2. Corregir seguridad de actualizaciones, yt-dlp y almacenamiento/transporte de claves.
3. Corregir sincronización de `DeckPlayer`, sampler, grabadora y ciclo de vida de callbacks.
4. Crear pruebas de parser, BPM/tonalidad, ring buffer, selección AutoMix y estados de descarga.
5. Mejorar documentación de privacidad, requisitos, limitaciones y matriz de plataformas.

No se encontró evidencia suficiente en esta revisión para concluir que la aplicación sea malware. Los hallazgos anteriores son defectos o riesgos de ingeniería y licencia, no una atribución de intención.
