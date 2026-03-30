# VelvetUI - Roadmap

**Estado actual: Fase 3 funcional - floating + liquid glass estables, en etapa de pulido**

## Ultima actualizacion: 30/03/2026

## Objetivo visual
- Conseguir una taskbar tipo dock/liquid glass:
- La ventana completa debe achicarse de verdad y separarse del borde inferior.
- El material no debe verse como una franja negra opaca full-width.
- El resultado buscado se parece mas a una capsula flotante estilo Apple que a una barra tradicional con blur encima.

## Diagnostico real

### Problema 1: Geometria
- El bloqueo principal no es el blur.
- El taskbar sigue ocupando todo el ancho del monitor porque el shell conserva el control de la geometria base.
- Mover `Shell_TrayWnd` tarde con `SetWindowPos` o con subclassing de `WM_WINDOWPOSCHANGING` no alcanza por si solo.
- El approach correcto necesita una capa de hooks de geometria, no solo cambios XAML.

### Problema 2: Material visual
- El glass actual funciona tecnicamente, pero hoy se ve demasiado oscuro y pesado.
- `CreateHostBackdropBrush()` + tint oscuro producen una franja tipo "glass negro", no una capsula ligera.
- En el codigo actual, `blurAmount`, `saturation` y `noiseOpacity` todavia no tienen efecto real sobre el resultado visual.

## Estado real del repo

### Lo que ya funciona
- Inyeccion del payload en `explorer.exe`.
- Conexion a `IXamlDiagnostics` via `InitializeXamlDiagnosticsEx`.
- `VisualTreeWatcher` enumera y filtra elementos del taskbar.
- `TaskbarModifier` aplica cambios XAML a:
- `RootGrid`
- `BackgroundFill`
- `BackgroundStroke`
- `ScreenEdgeStroke`
- `SystemTrayFrameGrid`
- El efecto actual via Composition se aplica sobre `RootGrid`.
- `config.json` se copia junto a `velvet.dll`.
- La altura interna del taskbar ya no queda clavada en `48px`; la cadena de `frame-size` del shell responde al alto configurado.
- La capsula principal ya se ve flotante y separada del borde inferior con un layout estable en la barra principal.

### Lo que hoy no esta resuelto
- Ajustes finos del tray derecho (`fecha/hora`, alineacion y distancia al borde).
- Geometria consistente para flyouts/rects consultados por el shell en todos los escenarios.
- Material visual todavia afinable para acercarlo mas a Lucent.
- Soporte multi-monitor.

## Estrategia correcta por capas

### Capa 1: Geometria Win32 real
- Resolver la taskbar correcta del proceso actual, no confiar en `FindWindow(L"Shell_TrayWnd")` a secas.
- Integrar MinHook para poder interceptar:
- `SetWindowPos`
- `GetWindowRect`
- Usar esos hooks como primera base operativa para que la ventana del taskbar deje de ser full-width.
- Mantener el subclassing viejo solo si sirve como correccion final; no como estrategia principal.

### Capa 2: Layout XAML
- Ajustar `TaskbarFrame` y `RootGrid` para que el contenido se comporte como dock:
- `Width=Auto`
- `HorizontalAlignment=Center`
- margenes/padding/corner radius correctos
- Esta capa sola no hace flotar la barra, pero mejora mucho el resultado una vez que la geometria Win32 ya esta controlada.

### Capa 3: Material
- Bajar el tint oscuro.
- Favorecer un look mas claro, mas sutil y menos opaco.
- Mantener `CreateHostBackdropBrush()` como baseline funcional.
- Dejar para una fase posterior el material mas avanzado:
- blur/saturation custom reales
- noise
- efectos mas complejos o brushes tipo Acrylic si conviene

### Capa 4: Geometria interna del shell
- Si `SetWindowPos` + `GetWindowRect` no alcanzan para estabilidad total, avanzar a hooks internos del taskbar:
- `TrayUI_GetDockedRect`
- `TrayUI_MakeStuckRect`
- posiblemente otros helpers del shell
- Esta es la direccion mas robusta, inspirada en los mods serios de Windhawk.

## Implementacion inmediata en VelvetUI

### Paso A
- Integrar MinHook de forma nativa en CMake.
- Habilitar compilacion de C en el proyecto para las fuentes de MinHook.

### Paso B
- Reemplazar la busqueda simple de taskbar por enumeracion filtrada al proceso actual.
- El codigo corre dentro de `explorer.exe`, asi que la ventana correcta debe pertenecer al PID actual.

### Paso C
- Agregar hooks para:
- `SetWindowPos`
- `GetWindowRect`
- Recalcular una `RECT` flotante usando:
- `marginHorizontal`
- `marginBottom`
- altura real actual del taskbar
- monitor correspondiente

### Paso D
- Empezar a aplicar layout XAML mas parecido a dock en `TaskbarFrame` y `RootGrid`.

### Paso E
- Suavizar el glass actual:
- bajar `tintOpacity`
- bajar opacidad del borde
- reducir la sensacion de franja negra

## Direccion visual deseada
- No queremos una taskbar verde.
- La referencia visual es una capsula flotante completa, translucida, limpia y mas "light".
- Prioridades visuales:
- menos opacidad
- menos negro
- bordes redondeados consistentes
- mejor separacion del fondo
- sensacion de vidrio suave, no de overlay oscuro

## Referencias tecnicas
- Windhawk `taskbar-on-top.wh.cpp`
- Windhawk `taskbar-vertical.wh.cpp`
- Windhawk `windows-11-taskbar-styler.wh.cpp`
- Windows 11 Taskbar Styling Guide
- Issue de Windhawk sobre `FindWindow(L"Shell_TrayWnd")` devolviendo handles incorrectos
- RoundedTB como referencia de UX, no como arquitectura final

## Decisiones de arquitectura
1. MSVC-only.
2. C++/WinRT del Windows SDK.
3. VelvetUI va a separar claramente geometria y material.
4. El floating real no se va a resolver solo con XAML.
5. `SetWindowPos` directo sin hooks no es suficiente.
6. `CreateHostBackdropBrush()` sirve como baseline, pero no define el look final.
7. Primero estabilizar la barra principal del monitor principal.
8. Multi-monitor y flyouts van despues de fijar la geometria base.

## Fases

### Fase 0: Setup - COMPLETADA
- [x] Repo, scaffold, injector y payload base
- [x] Compilacion inicial
- [x] Inyeccion en procesos de prueba

### Fase 1: Reconocimiento - COMPLETADA
- [x] Deteccion de explorer y ventanas del shell
- [x] Verificacion de estabilidad basica

### Fase 2: Visual Tree Access - COMPLETADA
- [x] `VisualTreeWatcher`
- [x] `IXamlDiagnostics`
- [x] cache de elementos
- [x] retry de endpoint

### Fase 3: Floating + Liquid Glass - FUNCIONAL

#### Ya hecho
- [x] `config.h`
- [x] `config_loader.h/.cpp`
- [x] `config.json`
- [x] `taskbar_modifier.h/.cpp`
- [x] estilos XAML base
- [x] glass baseline por Composition

#### En curso ahora
- [x] Integrar MinHook en el build
- [x] Resolver `Shell_TrayWnd` correcto del proceso actual
- [x] Hookear `SetWindowPos`
- [x] Hookear `GetWindowRect`
- [x] Sintetizar una `RECT` flotante estable
- [x] Ajustar `TaskbarFrame`/`RootGrid` para layout tipo dock
- [x] Bajar el glass oscuro actual
- [x] Confirmar por logs que la `RECT` Win32 sintetizada responde al perfil flotante
- [x] Validar el comportamiento real dentro de `explorer.exe`
- [x] Mover VelvetUI hacia la cadena interna de altura del shell con hooks de `frame-size` / `update frame`
- [x] Resolver simbolos internos con backend DIA estilo Windhawk
- [x] Estabilizar la ruta interna con modo seguro para evitar crashes de explorer por hooks ambiguos
- [x] Ajustar la capsula visual XAML para que el floating se perciba de verdad en pantalla
- [ ] Medir si la capa API (`SetWindowPos`/`GetWindowRect`) alcanza o si hay que pasar a hooks `TrayUI_*`
- [ ] Afinar el bloque derecho para mover fecha/hora un poco mas hacia el borde
- [ ] Revisar si hace falta una ultima pasada de limpieza/reduccion de logs internos

#### Luego
- [ ] Evaluar hooks internos del shell (`TrayUI_*`) si la API layer sola no alcanza
- [ ] Ajustar flyouts, overflow y otros popups
- [ ] Soporte para monitor secundario

### Fase 4: Servicio / resiliencia
- [ ] Reinyeccion si explorer reinicia
- [ ] arranque automatico
- [ ] deteccion rapida de recreacion del shell

### Fase 5: GUI
- [ ] sliders / perfiles
- [ ] hot reload
- [ ] system tray app

### Fase 6: Pulido
- [ ] code signing
- [ ] instalador
- [ ] release

## Notas importantes para futuras sesiones
- La barra principal ya esta en un estado usable y visualmente aprobado; priorizar retoques chicos antes que volver a tocar la base.
- No seguir empujando solo el blur: primero geometria.
- No confiar en que `TaskbarFrame.Margin` achique la ventana por si solo.
- No usar `FindWindow` simple para `Shell_TrayWnd`.
- Mantener el resultado visual apuntando a "capsula ligera", no "barra negra con blur".
- Estado comprobado al 30/03/2026:
- la barra principal ya usa altura interna consistente y una capsula flotante visible
- el modo seguro omite hooks ambiguos (`UpdateFrameHeight`, `SystemTrayController::UpdateFrameSize`) para privilegiar estabilidad
- el siguiente detalle concreto a tocar es `SystemTrayFrameGrid` para empujar fecha/hora un poco mas hacia el borde derecho
- si mas adelante hace falta robustez extra, la siguiente capa fuerte sigue siendo `TrayUI_GetDockedRect` / `TrayUI_MakeStuckRect` / `TrayUI_GetStuckInfo`
