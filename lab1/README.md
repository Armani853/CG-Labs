# Лабораторная работа №1: правильный октаэдр

Вариант 2 (`((14 - 1) % 12) + 1 = 2`). Программа написана на C++ с Vulkan,
GLFW и ImGui на основе выданного стартового проекта.

## Сборка на Windows

Нужны Vulkan SDK с `glslc`, CMake 3.20+, Git и Visual Studio 2022 с C++
или MinGW. При первой настройке CMake скачивает GLFW, vk-bootstrap, VMA и
ImGui с GitHub. Рекомендуется короткий путь к проекту, например
`C:\cg\lab1`.

В PowerShell из корня проекта (укажите свой путь к Vulkan SDK):

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.350.0'
$env:PATH = "$env:VULKAN_SDK\Bin;$env:PATH"
cmake --preset msvc-debug
cmake --build build-debug --config Debug --parallel 6
.\build-debug\Debug\vulkan-starter-app.exe
```

Для MinGW используйте `mingw-debug`, команду
`cmake --build build-debug --parallel 6` и файл
`build-debug\vulkan-starter-app.exe`. Между сборками MSVC и MinGW используйте
разные каталоги сборки. Приложение запускается из корня проекта, поскольку
загружает `shaders/*.spv` по относительному пути. CMake компилирует шейдеры
автоматически.

## Управление

- **Projection:** Perspective или Orthographic и параметры камеры.
- **Octahedron:** положение, поворот, масштаб и цвет Tint для каждого объекта.
- **Animation:** Play/Pause, Restart, скорость, радиус и частота орбиты,
  вертикальная амплитуда и частота, скорость вращения.
- **Object count:** число октаэдров от 1 до 4.
- Размер окна можно менять; закрытие — обычной кнопкой окна.

## Как устроено

В `source/application.cpp` создаётся сетка из 6 вершин и 8 треугольников.
Модельная матрица собирается как `T · Rz · Ry · Rx · S`; в шейдер передаётся
`P · V · M`. Цвет вершины вычисляется из её локальной позиции по формуле
`0.5 · (position + 1)` и умножается на выбранный Tint.

Анимация складывается из движения по орбите в плоскости XZ, вертикального
синусоидального смещения и собственного вращения. Для каждого объекта есть
отдельный uniform-буфер и `VkDescriptorSet`; сетка и pipeline общие.
`source/graphics_internal.cpp` содержит основу Vulkan: окно, swapchain,
глубину, синхронизацию кадров и обработку изменения размера окна.

Условия использования стартового проекта сохранены в `LICENSE` и `NOTICE`.
