# Лабораторная работа №3 — текстурирование

Вариант 2: правильный октаэдр (номер в списке 14, всего 12 вариантов). Проект
продолжает ЛР №2 и использует C++20, Vulkan, GLFW и ImGui.

## Сборка и запуск на Windows

Установите Visual Studio 2022 с компонентом C++, CMake 3.20+, Git и Vulkan SDK
с `glslc`. Для первого запуска CMake требуется интернет: он загружает GLFW,
vk-bootstrap, VMA и ImGui. `stb_image.h` уже лежит в `source`.

Откройте PowerShell в папке `lab3`:

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.350.0'
$env:PATH = "$env:VULKAN_SDK\Bin;$env:PATH"
$env:VK_LAYER_PATH = "$env:VULKAN_SDK\Bin"
cmake --preset msvc-debug
cmake --build build-debug --config Debug --parallel
.\build-debug\Debug\lab3-texture-mapping.exe
```

Подставьте установленную версию SDK. Запускайте EXE из папки `lab3`, потому
что он ищет шейдеры в `shaders/` и PNG в `textures/`. В отладочной сборке
запрашивается `VK_LAYER_KHRONOS_validation`, если слой установлен.

## Демонстрация

1. Откройте **Objects and material → Octahedron 1**. В **Texture** переключите
   `Ceramic mosaic`, `Frost crystal` и `Lava veins`. При переключении меняются
   альбедо, карта блеска и карта свечения.
2. Выберите `Lava veins` или `Frost crystal`. Уменьшите **Ambient intensity**
   и **Directional intensity**, затем ослабьте точечные и прожекторные источники.
   Светящиеся области сохраняются благодаря emissive карте.
3. Включите **Animated UV effect** у первого объекта. Сравните включённое и
   выключенное состояние, измените **UV distortion**. Шейдер берёт три сэмпла
   текстуры со смещёнными UV, смешивает и модулирует их со временем.
4. Увеличьте **Object count** до 3–4. Откройте настройки других октаэдров и
   назначьте им разные текстуры; первый объект может иметь отдельный эффект.
5. Покажите сохранённое из ЛР №2 управление камерой, светом, положением,
   поворотом и масштабом октаэдров. Измените размер окна и закройте его.

## Где находятся требования методички

- `source/application.cpp`: создание изображений Vulkan, staging буферы,
  дескрипторы материалов, динамический UBO и привязка с динамическим смещением,
  геометрия октаэдра с UV, ImGui.
- `shaders/lit.vert`: передача UV и преобразование координат.
- `shaders/lit.frag`: альбедо из PNG, блеск по specular карте, добавление
  emissive после освещения Блинна–Фонга, анимированное многократное
  сэмплирование по параметрам push constants.
- `textures/`: PNG файлы трёх материалов. При отсутствии specular или emissive
  карты привязывается служебное изображение с одним чёрным пикселем.

Для моделей используется один буфер `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`.
У каждого материала свой `VkDescriptorSet` с тем же буфером и тремя
`combined image sampler`. Смещение выбирается при `vkCmdBindDescriptorSets`.
Вершинные и индексные буферы создаются с предпочтением памяти GPU; данные
копируются туда через staging буфер и `vkCmdCopyBuffer`.

Условия использования стартового проекта сохранены в `LICENSE` и `NOTICE`.
