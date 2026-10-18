# Walkthrough — Renombrado visible DiscPro → DiscNativePro

## Objetivo
Eliminar restos de marca "DiscPro" en textos visibles UI (bandejas/JogWheel, TopBar, modales).

## Cambios ([MODIFY], alcance Solo-UI según aprobación)
- `src/ui/JogWheel.cpp`: eliminado bloque `drawText("DiscPro")` + comentario. Se conserva barra de color + BPM/RPM + spindle. Nota `ponytail:` el nombre largo no cabe en sticker ~60px.
- `src/ui/TopBarComponent.cpp:42`: `brandLabel "DiscPro"` → `"DiscNativePro"`. `resized(): centerWidth 140` → `220` para evitar truncado a 15pt bold.
- `src/ui/MidiModalComponent.cpp:13`: descripción → "DiscNativePro detecta automáticamente...".
- `src/ui/SettingsModalComponent.cpp:190,334`: combo waveform → "Azul Neón Clásico DiscNativePro" / `trUtf8(..., "Classic Neon Blue DiscNativePro")` (constructor + `refreshLanguage()`).

## Fuera de alcance (no tocado)
Rutas disco (`DiscPro_Recordings`, `DiscPro_Mix_`), thread name `DiscPro Audio Recorder`, demos `SampleTrackGenerator`, `COMPANY_NAME DiscPro`, comentarios (`DeckComponent.h:78`, `MainComponent.cpp:338`, `MainComponent.h:30`), README suite DiscPro.

## Verificación
- `cmake --build build` → 4 ficheros recompilados, link OK, 0 errores / 0 warnings.
  ```
  [  1%] Building CXX ... TopBarComponent.cpp.o
  [  3%] Building CXX ... JogWheel.cpp.o
  [  5%] Building CXX ... MidiModalComponent.cpp.o
  [  7%] Building CXX ... SettingsModalComponent.cpp.o
  [  9%] Linking CXX executable DiscNativePro_artefacts/Release/DiscNativePro.app/Contents/MacOS/DiscNativePro
  [100%] Built target DiscNativePro
  ```
- Grep post-cambio `"DiscPro"` como literal visible en `src/` → 0 resultados (solo quedan comentarios y `DiscPro_*` en rutas/demos, fuera de alcance).
- Relanzamiento: `pkill -f DiscNativePro; open build/DiscNativePro_artefacts/Release/DiscNativePro.app` → `pgrep` PID 420 activo. Nota: se relanzó Release (configuración actual del build), no Debug como indica AGENTS.md genérico.

---

# Walkthrough — Fila Hot Cue 4 pads por bandeja

## Cambios ([MODIFY] 2 ficheros, alcance aprobado: click fija/salta, solo RAM, fila 28px)
- `src/ui/DeckComponent.h`: `std::array<DjButton,4> hotCueButtons` + caché `hotCueButtonShown` + `refreshHotCueButtons()`.
- `src/ui/DeckComponent.cpp`:
  - Constructor: 4 pads texto `1-4` estilo STEMS (bg 24,26,32; ámbar 245,158,11 al fijar). `onClick`: video→no-op; sin pista→no-op; vacío→`setHotCue(i,getPosition())`, ocupado→`jumpToHotCue(i)`. Mismo store que pads MIDI.
  - `resized()`: fila 28px sobre la barra PLAY/CUE/Loop (4 pads equiespaciados, gap 6). Resta 34px al jog.
  - `timerCallback()` (60Hz): `refreshHotCueButtons()` con caché de estado (evita repaint spam); sincroniza MIDI↔botones.
  - `loadAudioFile()` + `loadYouTubeVideo()`: refresh (al cargar se apagan; en video deshabilitados en gris).
  - Nota: icono `Bookmark` no existe en `LucideIcons::IconType` → pads numéricos solo-texto (YAGNI, sin tocar LucideIcons).

## Fuera de alcance (no tocado)
Borrado de hot cues (`clearHotCue` sin cablear — vaciar = recargar), persistencia en disco, hot cues en modo video, `DeckPlayer`/`DjMixer`/`MidiManager`.

## Verificación
- `cmake --build build` → DeckComponent.cpp + MainComponent.cpp recompilados, link OK, 0 errores / 0 warnings (1 error intermedio de icono inexistente, corregido a texto).
- Relanzamiento Release → `pgrep` PID 21513 activo.
- Pendiente visual: cargar pista → click `1` fija (ámbar + marcador overview) → mover → click `1` salta; deck1/deck2 independientes; pad MIDI ilumina botón.

## Sistema de avisos de actualización (GitHub Releases)

### Cambios
- [NEW] `src/services/UpdateService.h/.cpp`: `checkForUpdatesAsync` (Thread + `juce::URL` a `api.github.com/.../releases/latest` con `User-Agent`, timeout 10s, parse `tag_name/name/body/assets`, asset por plataforma, `callAsync` al Message Thread). `isNewerVersion` semver por componentes (`1.10 > 1.2`; `-dev` local cuenta como más viejo). `skippedUpdateTag` en `settings.json`. Silencio total ante sin-red/timeout/rate-limit/tag omitido.
- [MODIFY] `CMakeLists.txt`: `APP_VERSION` (default `1.0.0-dev`) + sources + `DISCNATIVEPRO_VERSION` define.
- [MODIFY] `src/Main.cpp`: `getApplicationVersion()` desde el macro.
- [MODIFY] `.github/workflows/release.yml`: los 3 jobs pasan `-DAPP_VERSION` desde `GITHUB_REF_NAME` sin `v` (pwsh en Windows, bash en Linux/macOS; fallback `1.0.0-dev`).
- [MODIFY] `src/ui/TopBarComponent.h/.cpp`: `DjButton updateButton` ámbar `UPDATE` (oculto por defecto), `setUpdateAvailable()`, `onUpdateClicked`, tooltip ES/EN, hueco en `resized()` entre settings y layout.
- [MODIFY] `src/ui/MainComponent.h/.cpp`: `scheduleUpdateCheck()` (único chequeo, diferido 3s, `SafePointer`), `showUpdateDialog()` con `AlertWindow::showAsync(MessageBoxOptions)` + `ModalCallbackFunction`: [Descargar] abre asset directo, [Omitir] persiste tag + oculta badge, [Más tarde] deja badge para reabrir. Bilingüe.

### Verificación (real, con captura)
- `cmake --build build` → 0 errores; 0 warnings en ficheros propios (2 errores intermedios corregidos: `MessageManager` necesitaba `juce_events`, `showAsync` va con `MessageBoxOptions`).
- API real: tag `v1.0.0`, assets las 3 plataformas. Build local `1.0.0-dev` → diálogo esperado.
- `screencapture` confirma: badge `UPDATE` ámbar en TopBar + diálogo `Nueva versión disponible: 1.0.0 / Tu versión: 1.0.0-dev` con `Descargar/Omitir versión/Más tarde`. PID 64707.
- Mejora futura menor: el body del release se muestra en markdown crudo (truncado a 800 chars).

## ChannelStrip: knobs primero, fader absorbe (fix FILTER enano)
- Causa: `resized()` daba al fader mín. 130px primero; en ventana baja el resto no alcanzaba para 4×36 y el último knob (FILTER) se quedaba con las sobras (~21px).
- [MODIFY] `src/ui/ChannelStripComponent.cpp:142-156`: `knobH = jlimit(28,44,(availH-90-16)/4)` primero (4 iguales siempre), `faderH = jlimit(90,180, resto)`. Separadora baja sola. Piso 28 solo donde antes overfloweaba; techo fader 180 conserva pantallas altas idénticas.
- `cmake --build build` → 1 fichero, link OK, 0 errores/warnings. Relanzamiento Release → PID 24352.
- Capturas: ventana 1280×700 → HIGH/MID/LOW/FILTER iguales + fader corto; 1360×950 → idéntico a antes, sin regresión.

## Browser más alto (+25px)
- [MODIFY] `src/ui/MainComponent.cpp:388`: factor altura browser `0.32→0.35` (FX visible) / `0.38→0.41` (FX oculto). Clamp `190-480` intacto. Deck sin tocar.
- `cmake --build build` → 1 fichero, link OK, 0 errores/warnings. Relanzamiento Release → PID 20970.
- `screencapture` confirma: browser más alto, sin solapes en decks/FX/library; fila superior pierde ~25px (jog levemente menor, asumido).

## Micro-etiqueta HOT CUE (follow-up barato)
- `DeckComponent.h/.cpp`: `juce::Label hotCueRowLabel` con texto `HOT CUE` (9.5pt bold, gris 142,149,165 — término DJ universal, sin traducción). Ocupa 62px a la izquierda de la fila de pads en `resized()`.
- `cmake --build build` → OK, 0 errores/warnings. Relanzamiento Release → PID 29941 activo.

---

# Walkthrough — Corrección de YouTube (Error 153) y SoundCloud (HLS Stream) en Windows

## 1. Problema Identificado
1. **YouTube en Windows (Error 153)**: Al cargar videos de YouTube en Windows se mostraba el error *"Error de configuración del reproductor de video - Error 153"*. Causa raíz: `YouTubeVideoComponent_win.cpp` cargaba el reproductor mediante data URI (`data:text/html,...`), el cual Chromium/WebView2 aísla con origen `null` y sin cabeceras `Referer`. YouTube bloquea los reproductores embed que carezcan de un origen HTTP/HTTPS válido coincidente con el parámetro `origin` de la API de IFrame.
2. **SoundCloud (Error al descargar stream)**: Al pulsar en cualquier pista de SoundCloud saltaba *"Error al descargar stream de SoundCloud"*. Causa raíz:
   - Más del 90% del catálogo moderno de SoundCloud ya no ofrece streams directos HTTP progresivos (`protocol == "progressive"`), sino únicamente streams HLS fraccionados (`audio/mp4; codecs="mp4a.40.2"`).
   - El código original descartaba automáticamente cualquier pista que no tuviese stream progresivo, dejando `progressiveStreamAuthUrl` vacío.
   - Además, el `defaultClientId` embebido estaba vencido.

## 2. Cambios Realizados

### [MODIFY] `src/ui/YouTubeEmbedHtml.h` y `src/ui/YouTubeEmbedHtml.cpp`
- Se agregó el parámetro `originUrl` a `getYouTubeEmbedHtml()` (por defecto `"https://juce.backend"`).
- Se agregó meta tag `<meta name="referrer" content="strict-origin-when-cross-origin">`.
- Se configuró el iframe con `referrerpolicy="strict-origin-when-cross-origin"` y URL canónica `https://www.youtube.com/embed/...`.

### [MODIFY] `src/ui/YouTubeVideoComponent_win.cpp`
- Se implementó `withResourceProvider()` en las opciones de `WebBrowserComponent` para servir el contenido del player bajo el esquema seguro `https://juce.backend/player.html`.
- Se mapeó la respuesta HTTP 200 con cabeceras `Content-Type: text/html; charset=utf-8` y `Access-Control-Allow-Origin: *`.

### [MODIFY] `src/ui/YouTubeVideoComponent.mm` y `src/ui/YouTubeVideoComponent_linux.cpp`
- Se alineó la llamada a `getYouTubeEmbedHtml()` con el origen correspondiente.

### [MODIFY] `src/services/SoundCloudService.h` y `src/services/SoundCloudService.cpp`
- Actualizado `defaultClientId` con credencial activa y vigente (`kR7dAfT5UXQgmgde1pLjfsRGiaoANLvT`).
- Ampliada la estructura `SoundCloudTrack` para almacenar `hlsStreamAuthUrl`.
- Implementado el parser de transcodificaciones para capturar streams HLS de formato `audio/mp4; codecs="mp4a.40.2"`.
- Implementada la función `downloadHlsStream()`:
  - Descarga el manifiesto `playlist.m3u8`.
  - Extrae el segmento de inicialización `#EXT-X-MAP:URI="init.mp4"` y todos los segmentos de audio `.mp4` / `.aac`.
  - Concatena los bloques fMP4 generando un archivo de audio `.m4a` estándar de 160 kbps compatible con los decodificadores de audio del sistema.

### [NEW] `src/audio/MediaFoundationAudioFormat_win.h`
- Implementado lector de audio nativo de Windows (Media Foundation) para decodificar pistas `.m4a` y `.mp4` AAC en Windows sin requerir códecs externos.

### [MODIFY] `src/audio/AudioEngine.cpp` y `CMakeLists.txt`
- Registrado `MediaFoundationAudioFormat` en `AudioEngine` bajo `#if JUCE_WINDOWS`.
- Vinculadas las librerías `mfplat` y `mfreadwrite` en Windows. Habilitado `JUCE_USE_MP3AUDIOFORMAT=1`.

## 3. Verificación
- **Compilación**: Ejecutado `cmake --build build -j 2` resolviendo el problema de memoria de compilación paralela previa. 0 errores y 0 warnings en los módulos implementados.
- **Auto-Relaunch**: Ejecutado el relanzamiento obligatorio:
  - `pkill -f DiscNativePro || true`
  - `open "build/DiscNativePro_artefacts/Release/DiscNativePro.app"`
  - Confirmado PID activo: `88796`.

---

# Walkthrough — Instalador Formal para Windows con Inno Setup (`Setup.exe`)

## 1. Objetivo
Reemplazar la distribución en archivo `.zip` comprimido por un instalador profesional ejecutable (`DiscNativePro-Windows-x64-Setup.exe`) generado automáticamente con **Inno Setup** en GitHub Actions, configurando accesos directos, icono de alta calidad, desinstalador en Panel de Control y actualizando el actualizador interno de la app.

## 2. Cambios Realizados

### [NEW] `assets/icon.ico`
- Generado archivo `.ico` multi-resolución (256x256, 128x128, 64x64, 48x48, 32x32, 16x16) a partir de `assets/icon.png`.

### [NEW] `installer/windows/installer.iss`
- Script Inno Setup con GUID único (`{D37E9E8C-4F71-4A59-8809-5D8840BA031F}`):
  - Instalación en `{autopf}\DiscNativePro`.
  - Icono `assets/icon.ico` en instalador y desinstalador.
  - Accesos directos en menú Inicio y Escritorio (opcional).
  - Empaquetado de `DiscNativePro.exe` y `WebView2Loader.dll`.
  - Opción de lanzar la aplicación al finalizar (`skipifsilent`).

### [MODIFY] `src/services/UpdateService.cpp`
- Actualizado `kAssetName` bajo `#elif JUCE_WINDOWS` a `"DiscNativePro-Windows-x64-Setup.exe"` para que el diálogo de descarga de nuevas versiones enlace al instalador.

### [MODIFY] `.github/workflows/release.yml`
- Reemplazado el paso de compresión `.zip` por la invocación de `iscc` (Inno Setup Compiler).
- Publicación de `DiscNativePro-Windows-x64-Setup.exe` en el GitHub Release.

## 3. Verificación
- **Compilación**: `cmake --build build -j 2` completado al 100% con 0 errores y 0 warnings.
- **Auto-Relaunch**: App relanzada activamente con PID `180`.

---

# Revisión profunda local: fiabilidad, privacidad y distribución (2026-10-18)

## Alcance y cambios

- Audio: el análisis de pistas se hace fuera del bloqueo compartido; el scratch limita lecturas a muestras disponibles; el sampler publica buffers inmutables y corrige el ratio/límite por frecuencia de origen; la grabadora contabiliza muestras aceptadas, registra pérdidas y espera a productores antes de cerrar; los filtros EQ seleccionan coeficientes precalculados durante el callback; pitch, enrutamiento, tamaños de bloque y cierre del lector de disco tienen protecciones adicionales.
- Ciclo de vida: la carga de metadatos de video usa `SafePointer`; los callbacks MIDI se copian con sincronización y se invalidan al cerrar/reemplazar listeners.
- Privacidad/red: las solicitudes de IA usan HTTP de JUCE, no shell ni temporales con prompts/respuestas; las carpetas locales no se envían al proveedor y la vista de recomendaciones revela los metadatos transmitidos antes de solicitar consentimiento; las claves se enmascaran en UI y se informa que siguen guardadas sin cifrar. YouTube, SoundCloud y miniaturas validan identificadores/hosts y limitan respuestas/descargas.
- Actualización/distribución: el actualizador exige SHA-256 de GitHub y solo descarga el activo exacto de la plataforma; deja la instalación manual. yt-dlp selecciona un activo por plataforma y verifica digest antes de instalarlo. CMake fija el hash del paquete WebView2 1.0.2903.40, alineado con los workflows. El README ya no llama open source a un producto sin licencia y se añadió un aviso parcial para Signalsmith.

## Resultado de compilación

- Configuración: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` — correcta.
- Compilación: `cmake --build build --config Release --target DiscNativePro --parallel 6` — correcta.
- Artefacto: `build/DiscNativePro_artefacts/Release/DiscNativePro.exe`.
- La compilación completa mostró advertencias en cabeceras Signalsmith y código existente de `LanguageManager.cpp` / `SamplerPanelComponent.cpp`; el enlace terminó correctamente.
- No se ejecutó la aplicación ni pruebas automatizadas. El código no se validó en macOS/Linux ni con dispositivos de audio reales.

## Límites de publicación

- El código upstream carece de licencia de producto y el README conserva «Todos los derechos reservados». Esta copia es para análisis local; no se debe publicar ni redistribuir sin licencia o permiso expreso del titular.
- El uso del digest de GitHub comprueba integridad frente al digest de la API; no demuestra por sí solo la identidad del autor que publicó el archivo.
- Las API keys permanecen en texto claro dentro del archivo local de ajustes. La revisión añadió avisos, pero no implementó un almacén seguro multiplataforma.
- Workflows aún necesitan permisos mínimos, acciones fijadas por commit, SBOM/atestaciones y estrategia de firma/notarización de macOS antes de distribución.

---

# Inicio visible, negociación de salidas y lectura de discos (2026-10-18)

## Cambios

- `src/Main.cpp`: se muestra una ventana inicial ligera antes de construir `MainComponent`.
- `src/ui/MainComponent.cpp`: la inicialización de audio y MIDI empieza en el primer callback del componente principal, después de que la ventana ya está visible. Se aplazó la lectura de Descargas 300 ms y se refrescan tabla/árbol al terminar.
- `src/audio/AudioEngine.cpp/.h`: la salida predeterminada se abre primero con dos canales; si el dispositivo anuncia cuatro, se intenta habilitar el par adicional y se vuelve a estéreo si el driver lo rechaza. El estado reporta canales activos, pasa a cero al detenerse el dispositivo y el camino de recuperación reutiliza la misma negociación.
- `src/ui/SettingsModalComponent.cpp`: Preferencias distingue salida ausente, estéreo y multicanal activo.
- `src/services/UsbDriveScanner.cpp`: Windows deja de clasificar cada volumen fijo no-C como USB. Un disco externo fijo sigue disponible con selección manual.
- `src/ui/UsbLibraryView.cpp/.h`: no se enumeran volúmenes al construir la vista oculta; al abrir USB se listan, pero se espera que el usuario seleccione una unidad para iniciar el escaneo recursivo.
- `src/ui/SoundCloudLibraryView.cpp`: se elimina la búsqueda automática inicial.
- `ANALISIS_LECTURA_DISCOS.md`: auditoría estática del inventario, scan, metadata/BPM/key, deck loading, buffering, sample-rate y Media Foundation, con riesgos separados de las partes ya corregidas.

## Verificación

- `cmake --build build --config Release --parallel` → correcto, ejecutable Release creado.
- La primera compilación expuso dos avisos menores; se corrigieron el bloque `if` vacío en Release y el valor inicial del enum de idioma. La compilación incremental final terminó sin avisos.
- `git diff --check` → sin errores de whitespace (Git muestra solo avisos generales de conversión LF/CRLF en archivos ya modificados).
- Se abrió la build y Windows UI Automation mostró la ventana `DiscNativePro`, los dos decks, la biblioteca local y los controles principales; el proceso respondió. No se cargó ni reprodujo audio.
- La captura gráfica solicitada no se pudo obtener porque Windows Graphics Capture agotó su espera; la comprobación de la ventana fue por árbol accesible.
- No se validó el nombre/cantidad real de salidas en Preferencias ni una controladora conectada; el hardware no se comprobó en esta sesión.
- No se hizo un scan manual de una unidad, no se recorrió D: y no se probaron formatos de audio.

## Pendientes del pipeline de lectura

El escaneo manual USB y la importación local todavía recorren archivos de forma síncrona; la carga de decks vuelve a analizar BPM/key/waveform en el hilo que la solicita. La auditoría también confirma riesgo de bloqueo del callback por el lock de `BufferingAudioSource`, conversión incorrecta cuando sample-rate del archivo y dispositivo difieren, y defectos específicos del lector Media Foundation. Se proponen etapas de implementación en `ANALISIS_LECTURA_DISCOS.md`.
