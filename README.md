[![Latest version](https://img.shields.io/badge/latest-v0.4--dev-red.svg)](https://img.shields.io/badge/latest-v0.4--dev-red.svg) [![Join the chat at https://gitter.im/Banshee3D/Lobby](https://badges.gitter.im/Banshee3D/Lobby.svg)](https://gitter.im/Banshee3D/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

<<<<<<< HEAD
# What is Banshee? 
Banshee is a high-quality and modern game development toolkit. It provides a **high-performance, multi-threaded game engine** written in C++14. The engine includes math and utility libraries, Vulkan, DirectX 11 and OpenGL support, handles common tasks such as input, GUI, physics, audio, animation and scripting, and supports many popular resource formats (e.g. FBX, PNG, PSD, TTF, OGG, WAV).

Banshee provides an intuitive and customizable **editor** that can be used to manage assets, build levels, compile scripts, and to test and publish your game. The editor can be extended or customized with scripts to meet the exact needs of your project.

Banshee's **C# scripting** system comes with an extensive API that ensures development can be completed without ever needing to touch the engine's C++ core. C# scripting simplifies development by providing access to the entire .NET library, plus a wide variety of other managed libraries. The scripting system is fully integrated with the Banshee editor and external tools, such as Visual Studio. Fast compilation ensures iteration times between scripting and testing are minimized.

Banshee can also be used as a **low-level framework**, or as a powerful foundation to build new technologies upon. The engine can be easily customized for any game-specific needs. It uses a layered, plugin-based design that allows its functionality and systems to be upgraded, removed, or replaced as required. Banshee's code is modern, uses clean interfaces, and is easy to learn and maintain. Platform-specific functionality is kept at a minimum to make porting as easy as possible. It is fully documented with an extensive API reference. Introductory manuals are supplied for most major systems.

**WARNING: Banshee is still in development and should not be used in production.**
=======
# Что такое Banshee?

Современный инструментарий для разработки высококачественных современных игр. Он предоставляет **многопоточный игровой движок высокой производительности** написанный на языке C++14 для запуска 2D и 3D игр. Движок предлагает широкое разнообразие высокоуровневой системы необходимую для разработки игр, от математики и вспомогательных библиотек, до поддержки Vulkan, DirectX 11 и OpenGL, любых методов ввода, GUI, физики, звук, анимацию, скрипты и известные форматы русурсов (таких как FBX, PNG, PSD, TTF, OGG, WAV)

Вершиной движка Banshee является высокоинтуитивный и модифицируемый **редактор** которым могут легко пользоваться художники, дизайнеры и программисты. Редактор позволяет вам управлять вам всеми активными проектами, построением уровней, компиляцией скриптов, тестирование и публикацией ваших игр. Он также полностью расширяем, посредством скриптов, обеспечивая разработчиков легко доступным модифицированием под нужды своих проектов.

The scripting system supports C# and comes with an extensive API ensuring you can complete your game without ever touching the C++ engine core. **C# скрипты** makes your development easier by giving you access to the entire .NET library and a wide variety of pre-existing managed libraries. Integration of the scripting system with the editor and external tools like Visual Studio, as well as fast compilation times ensures that iteration times between coding and testing are minimized.

Система скриптов поддерживает C# и позволяет расширить API обеспечивая разработку игр не касаясь C++ игрового движка. **Скрипты C#** делают разработку проще, давая доступ к полному функционалу .NET библиотек и широкое разнообразие существующих управляемых библиотек. Интеграция системы скриптов с редактором и внешними инструментами как Visual Studio, такая же быстрая как время компиляции, обеспечивает минимальные затраты времени при повторном программировании и тестировании.

Помимо того что это полноценный игровой движок и инструментарий, Banshee так же используется как **низкоуровневая интегрированная структура (фреймворк)**, обеспечивающий мощную основу для создания новых технологий, а так же легко настриваемый движок для игр с конкретными потребностями. Конструкция основана на многослойности и расширяемости позволит разработчикам использовать только тот функционал который необходим, так же возможно полное удаление или изменение важных систем движка. Современный исходный код Banshe с прозрачным интерфейсом делает его легким в изучении и простым в обслуживании. По возможности мы минимизировали платформо-зависимый функционал. Справочники API полностью документированы так же как и руководства по основным системам.
>>>>>>> 6cec88ede54c7317741eb7ecea58f4e5b4afbafc

# Features
* [Core features](https://github.com/GameFoundry/bsf/blob/master/Documentation/GitHub/features.md) - A list of all core engine features.
* [Editor features](https://github.com/BearishSun/BansheeEngine/blob/master/Documentation/GitHub/features.md) - A list of all editor specific features.
* [Roadmap](https://github.com/GameFoundry/bsf/blob/master/Documentation/GitHub/roadmap.md) - A list of features to be implemented in both near and far future. 

# Download
* [Binaries](https://github.com/BearishSun/BansheeEngine/blob/master/Documentation/GitHub/install.md) - Download pre-built binaries for the most recent version.
* [Compiling](https://github.com/BearishSun/BansheeEngine/blob/master/Documentation/GitHub/compiling.md) - Learn how to compile the engine yourself.

# Documentation
<<<<<<< HEAD
* [Documentation](http://docs.banshee3d.com/Managed/index.html) - Documentation for the scripting (C#) API.
=======
* [Managed](http://docs.banshee3d.com/Managed/index.html) - Documentation for the scripting (C#) API. This is what most users will be interested in.
* [Native](http://docs.banshee3d.com/Native/index.html) - Documentation for the native (C++) API. This is meant for advanced users meaning to extend/modify the engine.
* [RUS Native](https://github.com/fascwind/BansheeEngine/blob/master/Documentation/RUS/Manuals/Native/index.md) - Документация на русском языке, главная цель данного перевода - изучение самого движка для дальнейшего его применения. Не является эталлоным. Перевод как метка моего изучения Banshee. пока не настроены все url. Передвижение внутри данной документации невозможен. Читайте оригинал)
>>>>>>> 6cec88ede54c7317741eb7ecea58f4e5b4afbafc

# About
* [License](https://github.com/BearishSun/BansheeEngine/blob/master/Documentation/GitHub/license.md) - Information about Banshee's license.
* [Contribute](http://www.banshee3d.com/contribute) - Help out with the development

# Media
**Banshee Editor**
![Banshee Editor](http://bearishsun.thalassa.feralhosting.com/BansheeEditor.png "Banshee Editor")

[**Video overview**](https://youtu.be/WJsYOyCXGEU)
