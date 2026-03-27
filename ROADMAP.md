# VelvetUI — Roadmap

**Estado actual: Fase 1 completa — reconocimiento de ventanas XAML**

## Última sesión: 26/03/2026
- Definimos arquitectura completa del proyecto (spec en FloatingTaskbar-SPEC.md)
- Investigamos técnicas: RoundedTB (descartado), Windhawk/TranslucentTB (elegido)
- Instalado VS 2026 Professional (MSVC 19.50 x64, CMake 4.2.3)
- Repo creado: github.com/brandonicardi/VelvetUI
- Scaffold: CMake + payload DLL + injector
- Inyección exitosa en notepad.exe y explorer.exe
- Reconocimiento: detectamos Shell_TrayWnd (1920x48), DesktopWindowXamlSource, XamlExplorerHostIslandWindow
- Encontramos el gist de m417z con la demo completa de VisualTreeWatcher + InitializeXamlDiagnosticsEx

## Próximo paso (Fase 2)
- [ ] Adaptar el VisualTreeWatcher del gist de m417z para VelvetUI
- [ ] Implementar interfaces COM: IVisualTreeService3, IXamlDiagnostics
- [ ] Implementar TAP (IObjectWithSite) + DllGetClassObject
- [ ] Llamar InitializeXamlDiagnosticsEx desde dentro de explorer.exe
- [ ] Recibir callbacks de OnVisualTreeChange
- [ ] Loguear los elementos XAML del taskbar (Type + Name)

## Fases

### Fase 0: Setup - COMPLETADA
- [x] Crear repo en GitHub
- [x] Instalar VS 2026
- [x] Crear scaffold del proyecto (CMake, DLL, injector)
- [x] Verificar compilación
- [x] Test de inyección en notepad.exe
- [x] Test de inyección en explorer.exe

### Fase 1: Reconocimiento - COMPLETADA
- [x] Inyección exitosa en explorer.exe
- [x] Enumerar ventanas hijas de Shell_TrayWnd
- [x] Detectar ventanas XAML (DesktopWindowXamlSource, XamlExplorerHostIslandWindow)
- [x] Verificar que explorer no crashea
- [x] Implementar eyección limpia

### Fase 2: Visual Tree access (SIGUIENTE)
- [ ] Implementar VisualTreeWatcher (basado en ExplorerTAP/m417z gist)
- [ ] Conectar a IXamlDiagnostics via InitializeXamlDiagnosticsEx
- [ ] Enumerar elementos del visual tree XAML
- [ ] Loguear nombres de elementos (TaskbarFrame, RootGrid, etc.)

### Fase 3: Modificación XAML
- [ ] Aplicar estilos de barra flotante (Height, Margin, CornerRadius)
- [ ] Lectura de config.json
- [ ] Hot-reload de config

### Fase 4: Servicio de Windows
- [ ] Servicio auto-start
- [ ] Re-inyección si explorer reinicia

### Fase 5: GUI de configuración
- [ ] WPF con sliders
- [ ] System tray icon
- [ ] Gestión de temas

### Fase 6: Pulido y release
- [ ] Instalador
- [ ] Documentación
- [ ] Release v1.0

## Referencia técnica
- Gist clave: https://gist.github.com/m417z/8741e52d8eaad67b47ee365a20070bf8
- ExplorerTAP: https://github.com/TranslucentTB/TranslucentTB/tree/develop/ExplorerTAP
- Taskbar Styler source: https://github.com/ramensoftware/windhawk-mods/blob/main/mods/windows-11-taskbar-styler.wh.cpp
- Styling guide: https://github.com/ramensoftware/windows-11-taskbar-styling-guide