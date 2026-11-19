# Лабораторная работа №4 — наложение теней

Вариант 2, правильный октаэдр. Сцена продолжает ЛР №3 и использует C++20,
Vulkan 1.3, GLFW и ImGui. Октаэдр отбрасывает тени на плиточный пол от
направленного света и от первого прожектора.

## Сборка и запуск на Windows

Нужны Visual Studio 2022 с компонентом «Разработка классических приложений на C++»,
CMake 3.20+, Vulkan SDK 1.3 или новее и драйвер с поддержкой Vulkan 1.3 и
`dynamicRendering`. При первой настройке CMake загружает фиксированные версии
GLFW, vk-bootstrap, VMA и ImGui.

Из папки `lab4` выполните в PowerShell:

```powershell
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.350.0'
$env:VK_LAYER_PATH = "$env:VULKAN_SDK\Bin"
cmake --preset msvc-debug
cmake --build build-debug --config Debug --parallel
.\build-debug\Debug\lab4-shadow-mapping.exe
```

Подставьте путь к установленному SDK. `VK_LAYER_PATH` помогает найти слой проверки
в отладочной сборке. CMake компилирует GLSL и размещает шейдеры и текстуры рядом
с исполняемым файлом. SDK нужен для сборки, а возможность запуска определяет
драйвер. Если приложение выводит `no_suitable_device`, проверьте версию API
физического устройства в `vulkaninfo --summary`.

На компьютере со старым драйвером можно использовать локальный Windows ICD,
например Mesa Lavapipe. Если в `local-vulkan/driver/` лежат его манифест
`icd.json` и указанные в нём библиотеки, CMake копирует их рядом с EXE.
Приложение выбирает этот драйвер только для своего процесса. Если SDK уже
известен CMake, достаточно трёх команд сборки и запуска. Локальный каталог
не входит в Git или архив исходников.

На Linux с X11, Vulkan Loader, драйвером Vulkan 1.3, заголовками Vulkan, CMake,
GLFW-зависимостями X11 и `glslc` или `glslangValidator`:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux --parallel
./build-linux/lab4-shadow-mapping
```

## Демонстрация

1. В разделе **Shadows** по очереди выключите **Sun shadows** и **Spot 1 shadows**.
   Каждая галочка отключает тень своего источника, сохраняя его освещение.
2. Оставьте включённой тень солнца. Уменьшите **Sun half extent** и посмотрите,
   как область карты перестаёт покрывать часть пола. При 6 единицах ширина
   квадратной области составляет 12 единиц.
3. Поставьте **Bias constant** и **Bias slope** в ноль для сравнения, затем
   нажмите **Reset shadow settings**.
4. В **Objects and material** включите **Animate objects**. Тень должна
   двигаться вслед за октаэдром. В **Spot lights → Spot 1** можно изменить
   положение и направление прожектора или показать его конус.

## Реализация

- `source/graphics_internal.cpp` запрашивает Vulkan 1.3 и включает
  `dynamicRendering` при выборе устройства.
- `createShadowResources()` в `source/application.cpp` создаёт две карты
  глубины 2048×2048 и сэмплер с `VK_COMPARE_OP_LESS`.
- `drawShadowMap()` записывает глубину через `vkCmdBeginRendering`. У
  конвейера теней одна вершинная стадия `shaders/shadow.vert`.
  Барьеры переводят карты между режимами записи и чтения.
- Для солнца используется `orthographic × lookAt`, а для прожектора
  `perspective × lookAt`. Угол перспективы равен удвоенному внешнему углу
  конуса прожектора.
- `shaders/lit.frag` получает видимость из `sampler2DShadow`, усредняет
  девять сравнений и умножает видимость на вклад соответствующего источника.
  Пол и октаэдры используют один основной конвейер освещения.

Условия использования стартового проекта сохранены в `LICENSE` и `NOTICE`.
