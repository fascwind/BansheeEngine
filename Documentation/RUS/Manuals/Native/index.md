Banshee Framework Documentation (Native)						{#mainpage}
===============

[TOC]

Welcome to the documentation for the Banshee Framework.
Добро пожаловать в раздел документации Banshee Framework.

This documentation contains a set of [manuals](@ref manuals) and an API reference for all native types.
Данный раздел состоит из руководств и описаний API для всех native типов.

# Manuals # {#mainpage_a}
Manuals should be your primary entry point into learning about Banshee. They can be split into two main categories:
Первый шаг в изучении Banshee должен быть через руководства. Их можно разделить на две категории:

 - User manuals - They explain how to use the provided API in order to create an application of your own, guiding you from basics through all the important systems. This is what most people will be interested in.
 -Рукодводство пользователя - Объясняют как происходит взаимодействие с API, если вы захотите самостоятельно создать приложение, показывая основы всех важных систем.

 - Developer manuals - They will provide you with a view of the general architecture of the framework, as well as the architecture of the more important systems. They will also teach you how to extend/modify various parts of the framework, including adding custom GUI types, script objects, resources, importers, renderers and many more. They are intended for those planning to extend or modify the framework in some way.
- Руководство разработчиков - Данные руководства дадут вам представление о общей архитектуре фреймворка, включая добавления своих типов графического интерфейса GUI, скриптовых объектов, русорсов, импортов, рендеров и прочего. Все это предназначено для планирования расширения и изменения фреймоврка теми или иными способами.

[Visit](@ref manuals) 
 
# API Reference # {#mainpage_b}
Справочник API

API reference provides a categorized and hierarchical view of all the framework's classes. 
Справочник API обеспечивает категоризированное и иерархическое представление о всех классах фреймворка

All classes are categorized into three primary groups:
Все классы делятся на три основные категории:
 - <a class="el" href="group___layers.html">Core API</a> - Contains documentation for the user-facing API of the framework core, categorized per layer. Each layer 
 is built directly on top of the previous one. This is what most users will be interested in. 
 - <a class="el" href="group___layers.html">Core API</a> - Ядра фреймворка содержит документацию о пользовательском API, классифицированного по слоями. Каждый слой построен поверх предидущего. Это должно инетерсовать пользователей больше всего.
 - <a class="el" href="group___internals.html">Internals</a> - Reference documentation for internals of the framework core, categorized per layer. Primarily useful for those modifying the framework but of less relevance for normal users.
 - <a class="el" href="group___internals.html">Internals</a> - Справочная документация для внутреннего строения ядра, классифицировано по слоям. В основном используется для изменения фреймворка, но менее актуальны для обычных пользователей.
 - <a class="el" href="group___plugins.html">Plugins</a> - Reference documentation for all available plugins. Plugins are various interchangeable libraries that contain high level systems built on top of abstractions defined in the framework core. If you are modifying the framework you might be interested in this documentation, but it can be skipped for most normal users.
 - <a class="el" href="group___plugins.html">Plugins</a> - Справочная информация о всех возможностей расширений. Расширения (плагины) это различные взаимозаменяемые библиотеки которые содержат высокий уровень сборки на вершине абстракций в ядре. Если вы хотите изменить фреймворк вас может заинтересовать эта документация, но ее можно пропустить для обычных пользователей
 
You should read the [architecture](@ref architecture) manual for a more detailed breakdown of the architecture.
Вы должны прочитать [architecture](@ref architecture) руководство для детального разбора архитектуры.