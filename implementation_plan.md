# Plan de revisión y endurecimiento local

## Ampliación autorizada: inicio, salidas y lectura de discos

El usuario pidió que la ventana se muestre primero, que luego se inicialicen y lean las salidas de audio disponibles, y una revisión profunda de cómo está construido el flujo de lectura de discos. La instrucción directa del usuario autoriza estos cambios; no hace falta una aprobación adicional del plan.

### Objetivo y contexto

- Mostrar primero una ventana ligera de inicio y construir la interfaz principal después de que Windows haya podido presentarla.
- Retrasar el inicio de audio/MIDI hasta que la interfaz esté visible. Abrir inicialmente dos canales y ampliar a cuatro solo si el dispositivo confirma que puede abrirlos, conservando el modo estéreo si no puede.
- Evitar que el escáner USB incluya automáticamente volúmenes fijos secundarios como D: y evitar lecturas recursivas al arrancar o al entrar por primera vez en la biblioteca USB.
- Revisar por separado el recorrido de enumeración, escaneo, metadata, streaming a decks y callback de audio; documentar riesgos confirmados y recomendaciones.

### Cambios previstos

- `[MODIFY] src/Main.cpp`: presentar un estado de inicio y aplazar la construcción del componente principal.
- `[MODIFY] src/ui/MainComponent.cpp`, `src/ui/MainComponent.h`: inicializar dispositivos después de mostrar la UI y diferir el escaneo de Descargas.
- `[MODIFY] src/audio/AudioEngine.cpp`, `src/audio/AudioEngine.h`: negociar estéreo primero, intentar cuatro canales con fallback, reflejar los canales activos y reutilizar el camino al recuperar el dispositivo.
- `[MODIFY] src/ui/SettingsModalComponent.cpp`: diferenciar salida sin dispositivo, estéreo activo y salida multicanal activa.
- `[MODIFY] src/services/UsbDriveScanner.cpp`: no clasificar todos los volúmenes fijos distintos de C: como USB.
- `[MODIFY] src/ui/UsbLibraryView.cpp`, `src/ui/UsbLibraryView.h`: dejar de enumerar unidades al construir la vista y pedir selección explícita de una unidad para iniciar su lectura.
- `[MODIFY] src/ui/SoundCloudLibraryView.cpp`: evitar una búsqueda de red automática durante la construcción de la biblioteca.
- `[NEW] ANALISIS_LECTURA_DISCOS.md`: documentar etapas del pipeline, problemas confirmados, riesgos condicionales y arquitectura recomendada.
- `[MODIFY] walkthrough.md`: registrar cambios, auditoría del flujo de lectura y resultado de compilación/inspección visual.

### Verificación

1. Revisar el diff y `git diff --check` para asegurar que los cambios no alteren el trabajo previo.
2. Compilar la configuración `Release` de Windows ya configurada.
3. Abrir la aplicación compilada y confirmar que aparece una ventana con la interfaz principal.
4. No reproducir canciones ni recorrer D: completo durante la verificación. La auditoría de lectura de pistas será estática y distinguirá lo confirmado de lo condicional.

## Objetivo

Revisar la copia local de DiscNativePro con foco en fiabilidad del audio, privacidad de datos y distribución. Aplicar correcciones locales acotadas a defectos confirmados, documentar lo que requiera decisión/licencia upstream y después intentar la compilación de Windows disponible en este equipo.

## Alcance

- Revisar rutas de audio en tiempo real, sincronización, grabación y tamaños de bloque.
- Revisar servicios de red, claves, solicitudes de IA, cachés, procesos externos y actualizaciones.
- Revisar licencias, dependencias y workflows de empaquetado por plataforma.
- Corregir defectos confirmados de seguridad/robustez que no necesiten decisiones del titular upstream.
- No publicar ni redistribuir la copia. La licencia del código del producto está sin resolver.

## Componentes y archivos

- `[MODIFY] src/services/AIService.cpp`: enviar solicitudes con HTTP de JUCE, minimizar metadatos locales y retirar archivos temporales/dumps.
- `[MODIFY] src/services/ThumbnailCache.cpp`, `src/services/YouTubeService.cpp`: eliminar comandos de shell para obtener imágenes/búsquedas y validar entradas.
- `[MODIFY] src/services/UpdateService.cpp`: evitar ejecutar instaladores descargados sin verificación criptográfica.
- `[MODIFY] src/audio/DeckPlayer.cpp`, `src/audio/AudioRecorder.cpp`, `src/audio/SamplerBank.cpp`: remediar defectos localizados de límites, sincronización y contabilización.
- `[MODIFY] UI aplicable`: informar antes de enviar datos de biblioteca a un proveedor IA y enmascarar claves.
- `[NEW] implementation_plan.md`, `[MODIFY] walkthrough.md`: registrar la revisión, decisiones y resultado de build.

## Verificación

1. Revisar el diff y confirmar que no queden comandos shell en las rutas de red modificadas.
2. Configurar CMake para Windows con las dependencias disponibles y compilar `Release`.
3. Si el host no tiene toolchain/dependencias necesarias o el código no compila, conservar el error exacto y reportar el bloqueo.

## Resultado de esta revisión

- [x] Revisión estática profunda de audio, grabación, MIDI, servicios de red, privacidad y distribución; se usaron tres agentes para revisar fiabilidad, privacidad y distribución en paralelo.
- [x] Correcciones locales de límites y sincronización en `DeckPlayer`, `SamplerBank`, `AudioRecorder`, `DjMixer`, `AudioEngine` y callbacks de MIDI/UI.
- [x] Solicitudes de IA, miniaturas, búsquedas de YouTube y SoundCloud dejan de invocar `system()`, `popen()` o `curl`; las descargas están limitadas y validan entradas/hosts donde aplica.
- [x] Descargas de actualización y yt-dlp requieren SHA-256 del campo `digest` de GitHub; los instaladores ya no se ejecutan automáticamente.
- [x] CMake fija el hash SHA-256 de WebView2 1.0.2903.40 y la versión coincide con CI.
- [x] `git diff --check` y compilación Release de Windows completados.

### Pendientes que bloquean publicación o una afirmación de privacidad completa

- El producto upstream no tiene una licencia que autorice redistribución o derivados; la copia permanece local.
- Las claves API todavía persisten en `settings.json` sin cifrado del sistema operativo.
- El hash de GitHub detecta corrupción respecto del digest publicado, pero no sustituye una firma verificable independiente del mismo servicio.
- No se compilaron ni probaron macOS/Linux, hardware de audio, YouTube/SoundCloud reales ni el instalador; tampoco se ejecutaron pruebas automatizadas.
- Falta cerrar inventario/SBOM y endurecimiento de workflows (acciones fijadas por SHA, permisos mínimos, atestaciones); macOS sigue sin firma/notarización de Developer ID.
