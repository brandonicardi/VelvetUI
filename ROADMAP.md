# VelvetUI — Roadmap

**Estado actual: Fase 0 — Scaffold completo, pendiente compilar y testear**

## Última sesión: 26/03/2026
- Definimos arquitectura completa del proyecto
- Investigamos técnicas de inyección y modificación XAML
- Creamos spec completo (FloatingTaskbar-SPEC.md)
- Instalado VS 2026 Professional (MSVC 19.50 x64, CMake 4.2.3)
- Repo creado: github.com/brandonicardi/VelvetUI
- Scaffold del proyecto creado (CMake + payload DLL + injector)

## Próximo paso
- [ ] Compilar el proyecto con CMake
- [ ] Verificar que velvet.dll y VelvetUI.exe se generan
- [ ] Testear inyección en notepad.exe (test seguro)
- [ ] Verificar logs en DebugView
- [ ] Testear inyección en explorer.exe

## Fases

### Fase 0: Setup ✅ (en progreso)
- [x] Crear repo en GitHub
- [x] Instalar VS 2026
- [x] Crear scaffold del proyecto (CMake, DLL, injector)
- [ ] Verificar compilación
- [ ] Test de inyección básica

### Fase 1: Inyección funcional
- [ ] Inyección exitosa en explorer.exe
- [ ] Verificar que explorer no crashea
- [ ] Implementar eyección limpia

### Fase 2: Visual Tree access
- [ ] Hook CreateWindowExW
- [ ] Conectar a IXamlDiagnostics
- [ ] Enumerar elementos del visual tree

### Fase 3: Modificación XAML
- [ ] Aplicar estilos de barra flotante
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
