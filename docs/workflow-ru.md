# DGX-GE: путь игры от данных до платы

[English version](workflow-en.md)

Движку всё равно, откуда берутся данные игры. Картинки можно нарисовать в
любом редакторе, скрипты анимации написать руками, уровни сгенерировать
своей программой, а сцены разложить в редакторе. Для движка всё это
становится одним и тем же: пакетом ресурсов, который лежит во flash и
читается на месте, и заголовком C, где у каждой вещи есть номер. Сам я
делаю данные в своём редакторе спрайтов (он будет опубликован отдельно), но
движок к нему не привязан, и мне было важно, чтобы так и оставалось:
редактор — один из способов, а не обязательный путь.

Этот текст описывает путь игры с точки зрения движка. Есть данные, неважно
чьи; что с ними делать дальше, шаг за шагом, пока игра не пойдёт на плате.
Дверь в движок одна — упаковщик `tools/dgx_ge_pack.py`. К нему ведут три
входа: свой скрипт на Python, документ редактора в формате, который
упаковщик читает, или то и другое вместе. Дальше путь общий: пакет и
заголовок, код игры на C, сборка для платы и для компьютера, проверка, плата.

Как устроен сам движок, рассказано в [обзоре](overview-ru.md), язык
скриптов анимации — в [animation-ru.md](animation-ru.md), сборка и запуск
игры на компьютере — в [host-ru.md](host-ru.md). Самая маленькая целая игра
лежит в [`examples/hello`](../examples/hello/README.md); общее описание
движка — в [README](../README.md). Здесь примеры называются `mygame`: это
выдуманная игра, а не та, что поставляется.

**Содержание**

1. [Картина целиком](#1-картина-целиком)
2. [Данные: три входа в упаковщик](#2-данные-три-входа-в-упаковщик)
3. [Что выходит из упаковщика](#3-что-выходит-из-упаковщика)
4. [Пакет заново сам](#4-пакет-заново-сам)
5. [Сторона C](#5-сторона-c)
6. [Сборка](#6-сборка)
7. [Цикл работы](#7-цикл-работы)
8. [Если что-то пошло не так](#8-если-что-то-пошло-не-так)
9. [Как проверен этот текст](#9-как-проверен-этот-текст)

---

## 1. Картина целиком

Игра на DGX-GE — это данные и немного кода. Данные говорят, что есть в игре
и как оно себя ведёт; код говорит то, чего данные сказать не могут. Главное
правило движка: данных как можно больше, специального кода на C как можно
меньше (обзор, [«Игра — это данные»](overview-ru.md#игра--это-данные)).

Из чего состоят данные и как каждая часть попадает в пакет (`<P>` — приставка
пакета, например `MYGAME`):

| Часть | Что это | Чем кладётся в пакет | Что видит код C |
| --- | --- | --- | --- |
| Картинки | PNG с палитрой, 1, 4 или 8 бит на точку, цвет 0 прозрачен; набор — картинки с общими цветами; базовая точка («где ноги»); маски столкновений; точки привязки («где рука»); вырезки | `add_set`, `add_png` (`add_image` для готовых значений), `add_mask`, `name_mask`, `add_meets`, `add_point`, `add_crop` | `<P>_SET_*`, `<P>_IMG_*`, `<P>_MASK_*`, `<P>_POINT_*` |
| Анимации | скрипты `.anim` на языке движка: кадры на время, движение, события, параметры, правила переходов | `add_anim`, `add_anims` | `<P>_BLOB_<ВИД>`, `<P>_<ВИД>_SCRIPT_*`, `_EVENT_*`, `_PARAM_*`, `<P>_EVENT_*` |
| Плитки и сцены | наборы плиток с правилами; сцены из слоёв, картинок, персонажей, зон, входов и сеток | `add_tileset`; сцены — из документа редактора (`add_sprites`) | `<P>_<НАБОР>_TILE_*`, `_SLOT_*`, `<P>_SCENE_<СЦЕНА>_*` |
| Граф сцен | какие сцены есть, как из одной попасть в другую, правила карты, связи между клетками | `add_graph` (класс `Graph` из `tools/dgx_ge_graph.py`), `add_links` | `<P>_<ГРАФ>_EXIT_*`, `_ENTRY_*`, `_MAP_*` |
| Диалоги | `.dlg` на языке `tools/dgx_ge_dialog.py`: кто что говорит, ответы, что от них меняется; переводы `.po`; поля ввода | `add_dialog`, `translate`, `add_field` | `<P>_BLOB_<ДИАЛОГ>`, `<P>_<ДИАЛОГ>_ROLE_*`, `<P>_FIELD_*` |
| Стили и шрифты | как выглядит разговор; какие шрифты нужны, запросами к DGX | `add_style`, `add_font` | `<P>_STYLE_*`, `<P>_FONT_*`, `<P>_FONTS(X)` |
| Числа игры | константы игры; имена чисел для скриптов; таймеры; звуки; заставки; погони; толпы | `add_game`, `add_constants`, `add_timer`, `add_sound`, `add_show`, `add_chase`, `add_crowd` | `<P>_GAME_*`, `<P>_<ИМЯ>` (имена чисел), `<P>_TIMER_*`, `<P>_SOUND_*`, `<P>_BLOB_*` |
| Сырые байты | всё, что игра читает сама: уровень своего формата, таблица | `add_blob` | `<P>_BLOB_*` |

Код игры на C — это то, что остаётся: правила, которых данные не выражают
(куда целится враг, что такое «полная строка»), настройка платы, главный
цикл и ввод — какие кнопки становятся какими событиями.

Путь целиком:

```
 PNG, .anim, .dlg, .po,                  документ редактора (JSON, "dgx-ge-sprites"),
 числа игры в самом скрипте              PNG, <ВИД>.anim, dialogs/*.dlg, po/*.po рядом
            │                                          │
            └──────────►  скрипт сборки на Python  ◄───┘
                          (Pack из tools/dgx_ge_pack.py)
                                       │  write, write_fonts, write_letters, write_pot
                                       ▼
   mygame.pack   mygame_pack.h   mygame.fonts   mygame.letters   mygame.pot   (mygame.sounds)
        │              │               │
        ├──────────────┼───────────────┤
        ▼              ▼               ▼
  прошивка (idf.py):                         компьютер (make, boards/host/host.mk):
  EMBED_FILES, dgx_ge_params_of_pack,        GAME_EMBED, GAME_PACK_H, GAME_FONTS
  dgx_ge_fonts → DGX делает шрифты           → DGX делает те же шрифты
        │                                          │
        ▼                                          ▼
  плата: пакет во flash, читается на месте   окно SDL2: дисплей платы, мышь, клавиши
```

Всё, что на схеме выше пакета, происходит на компьютере и один раз, при сборке:
проверка имён, перевод строк, сжатие звуков, шрифты. На плату приходит
готовое, и там ничего не разбирается и не копируется.

## 2. Данные: три входа в упаковщик

Упаковщик — это класс `Pack` из `tools/dgx_ge_pack.py`. Он принимает данные
вызовами `add_...`, проверяет всё, что можно проверить до платы, раздаёт
номера и пишет пакет и заголовок. Описание каждого вызова — в его
docstring, там же, в файле; язык анимаций описан в начале
`tools/dgx_ge_anim.py`, язык диалогов — в начале `tools/dgx_ge_dialog.py`,
граф сцен — в начале `tools/dgx_ge_graph.py`.

Нужны Python 3 и Pillow (`python3-pil` или `pip install Pillow`): Pillow
читает PNG. Упаковщик лежит в движке; из реестра ESP-IDF он приходит в
`managed_components/jef-sure__dgx-ge/tools/`.

### 2.1. Свой скрипт на Python

Самый прямой вход: скрипт игры сам находит свои файлы и вызывает упаковщик.
Так собран пакет `examples/hello` (`tools/hello_assets.py`); вот такой же
скрипт для `mygame`, с диалогом, шрифтом и звуком:

```python
# tools/make_pack.py: python3 tools/make_pack.py [<движок>]
import math, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
engine = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "managed_components", "jef-sure__dgx-ge")
sys.path.insert(0, os.path.join(engine, "tools"))
from dgx_ge_pack import Pack


def path(*parts):
    return os.path.join(ROOT, *parts)


pack = Pack("MYGAME")

# герой: цвета набора (значения 1, 2, 3 его картинок; цвета PNG находятся среди них по RGB)
# и картинки с базовой точкой у ног
hero = pack.add_set("HERO", [(230, 80, 60), (40, 30, 30), (250, 210, 160)])
for name in ("HERO_STAND", "HERO_STEP"):
    pack.add_png(name, hero, path("art", name.lower() + ".png"), origin=(5, 9))
ground = pack.add_set("GROUND", [(60, 140, 60), (110, 200, 90)])
pack.add_png("GROUND", ground, path("art", "ground.png"), origin="top-left")
pack.add_set("UI", [(150, 205, 250), (20, 40, 80)])            # небо и чернила: цвета без картинок

with open(path("anim", "hero.anim"), encoding="utf-8") as f:
    pack.add_anim("HERO", f.read(), path=path("anim", "hero.anim"))   # path: ошибки называют файл

pack.add_game(walk_ms=3000)                                    # число игры, C читает его по имени

rate = 22050                                                   # звук: отсчёты из скрипта или WAV
tick = [int(8000 * math.sin(2 * math.pi * 900 * t / rate) * (1 - t / 600)) for t in range(600)]
pack.add_sound("STEP", samples=tick, rate=rate)

pack.add_font("text", points=10)                               # шрифт — запрос к DGX
pack.add_style("talk", place=(4, 4, -8, 30), font="text", appear="letters", letter_ms=20)
lang = os.environ.get("MYGAME_LANG")
if lang:
    pack.translate(path("po", lang + ".po"))                   # язык пакета, до диалогов
with open(path("dialogs", "hello.dlg"), encoding="utf-8") as f:
    pack.add_dialog(f.read(), path=path("dialogs", "hello.dlg"))

out = path("main", "assets")
os.makedirs(out, exist_ok=True)
pack.write(os.path.join(out, "mygame.pack"), os.path.join(out, "mygame_pack.h"))
pack.write_letters(os.path.join(out, "mygame.letters"))
pack.write_fonts(os.path.join(out, "mygame.fonts"), os.path.join(out, "mygame.letters"))
pack.write_pot(os.path.join(out, "mygame.pot"))
```

Скрипт принимает путь к движку первым аргументом. Так его одинаково зовут
обе сборки (раздел 4), каждая со своим знанием о том, где движок.

Скрипт героя, `anim/hero.anim`, на языке движка: стоит, а когда игра
говорит `walk`, шагает, и каждый шаг — звук и сигнал игре:

```
event walk, stop
param strides 0

script stand
    frame HERO_STAND 1000ms
    goto stand

script walk
label stride
    sound 0                  # первый звук пакета, STEP
    frame HERO_STEP 120ms
    dx 2
    frame HERO_STAND 120ms
    dx 2
    add strides 1
    signal 1                 # игра слышит шаг
    branch
    goto stride

on walk: stand -> walk now
on stop: walk -> stand branch
```

И диалог, `dialogs/hello.dlg`:

```
dialog hello
roles hero HERO "Hero"
style talk
    say hero "Hello! I am going for a walk."
    end
```

Картинки — обычные PNG с палитрой из любого редактора; цвет 0 палитры
прозрачен.

**Порядок вызовов важен.** Упаковщик проверяет ссылки сразу, поэтому то, на
что ссылаются, добавляется раньше:

| Вызов | Что должно быть добавлено до него |
| --- | --- |
| `add_png`, `add_image` | набор (`add_set`) |
| `add_anim` | картинки, которые называют его кадры; точки (`add_point`); имена масок (`name_mask`); имена чисел (`add_constants`); виды, которые он называет (`appear`, `shoot`, `draw`, `sense`). Виды, которые знают друг друга (волк и овцы), добавляются вместе: `add_anims` |
| `add_tileset` с `life` | анимация жизни клетки |
| `add_dialog` | анимации видов, которые играют роли; перевод (`translate`); поля (`add_field`); картинки портретов |
| `add_show`, `add_crowd`, `add_chase` | анимации их видов |
| `add_style` с рамкой из картинки | сама картинка |
| `add_graph` | блобы данных его карт |

Таймер можно прочитать в скрипте (`timer(clock)`) раньше, чем он описан
`add_timer`, но описан он должен быть до `write`.

`write` пишет пакет и заголовок вместе и только целиком: если один из
файлов записать нельзя, оба старых остаются как были. Шрифты, буквы и шаблон
перевода пишутся отдельными вызовами (раздел 3), потому что нужны не каждой
игре. Предупреждения (цвет картинки, которого нет в наборе, взят ближайший)
упаковщик печатает в `stderr` и держит в `pack.warnings`.

### 2.2. Документ редактора: `add_sprites`

Второй вход — один вызов:

```python
pack = Pack("MYGAME")
pack.add_sprites("game_data/mygame.json")
pack.write("main/assets/mygame.pack", "main/assets/mygame_pack.h")
```

`add_sprites` читает документ формата `dgx-ge-sprites`: JSON, который пишет
мой редактор спрайтов. Но формат не принадлежит редактору. Его может
написать любой другой редактор, генератор уровней или скрипт, переводящий
данные из чужого формата, — упаковщик проверит его так же, как всё
остальное. Документ сам по себе — это разметка: картинки в нём не лежат, а
называются по именам файлов.

В общих чертах (полностью — в docstring `Pack.add_sprites` и в функциях,
которые он зовёт: `_add_layout`, `_links_of`, `_add_graph_of`, `_add_game_of`,
`add_tileset`, `add_style`; они описывают сам формат `dgx-ge-sprites`, поле
за полем, как его читает упаковщик):

| Поле | Что это | Что делает упаковщик |
| --- | --- | --- |
| `format`, `formatVersion` | `"dgx-ge-sprites"` и `1` | другой формат или версия новее — отказ |
| `masks` | `[{index, name}]`: какие маски есть | `name_mask`: `<P>_MASK_<NAME>` |
| `meets` | текст: какие маски встречаются | `add_meets` |
| `constants` | `{NAME: число}`: имена чисел для скриптов и условий диалогов | `add_constants`: в заголовке `<P>_<NAME>` с числом |
| `sets` | наборы кадров: `{name, defaultMask, frames: [{image, width, height, origin: {x, y}, masks, points, packName}]}` | набор цветов из палитр его PNG; каждый кадр — картинка `<НАБОР>_<ФАЙЛ>` (или `packName`) с базовой точкой, масками (строки битов в шестнадцатеричных цифрах), точками; кадр без масок в наборе с `defaultMask` получает маску этого номера из своих непрозрачных точек |
| `palettes` | наборы цветов без картинок | `add_set` |
| `tilesets` | `{name, cellWidth, cellHeight, tiles: [{name, kind, solid, pieces, ...}]}` | `add_tileset`; имена слоёв кусков становятся слотами |
| `styles` | стили разговоров | `add_style` |
| `screen` | `{width, height}`: экран игры | `<P>_SCREEN_WIDTH`, `_HEIGHT` |
| `scenes` | сцены: слои с объектами `picture`, `actor`, `zone`, `entry`; сетки плиток; выходы; связи | блоб `SCENE_<ИМЯ>` (формат — в `include/dgx_ge_layout.h`) и имена его объектов в заголовке |
| `graph`, `maps` | граф сцен и его карты | `Graph` и `add_graph`; связи карты — `add_links` |
| `game`, `timers`, `sounds`, `crops`, `shows`, `chases` | игра целиком | `add_game`, `add_timer`, `add_sound` (только имена), `add_crop`, `add_show`, `add_chase` |

Рядом с документом упаковщик ищет файлы, которых в JSON нет:

- картинки кадров — в `<набор>/<файл>.png` или прямо рядом;
- анимацию набора — `<набор>.anim` рядом с JSON (все такие анимации
  собираются вместе, `add_anims`, и знают друг друга); набор, чьё имя карта
  называет своими правилами, — не вид, а правила графа;
- диалоги — `dialogs/*.dlg`;
- перевод — `po/<язык>.po`, если до вызова сказать язык:
  `pack.language = "ru"`.

Небольшой документ — один набор, плитки, одна сцена:

```json
{
  "format": "dgx-ge-sprites", "formatVersion": 1, "name": "mygame",
  "masks": [{"index": 1, "name": "body", "color": "#e04040"}],
  "constants": {"STRIDE": 1},
  "sets": [
    {"name": "HERO", "defaultMask": 1, "frames": [
      {"image": "stand.png", "width": 10, "height": 10, "origin": {"x": 5, "y": 9}},
      {"image": "step.png", "width": 10, "height": 10, "origin": {"x": 5, "y": 9}}]},
    {"name": "WALL", "defaultMask": null, "frames": [
      {"image": "brick.png", "width": 8, "height": 8, "origin": {"x": 0, "y": 7}}]}],
  "tilesets": [{"name": "bricks", "cellWidth": 8, "cellHeight": 8, "tiles": [
    {"name": "brick", "kind": 1, "solid": {"top": true},
     "pieces": [{"set": "WALL", "image": "brick.png", "layer": "back"}]}]}],
  "screen": {"width": 160, "height": 100},
  "scenes": [{"name": "field", "width": 160, "height": 100,
    "layers": [
      {"name": "back", "factor": 256, "order": "added", "objects": [
        {"type": "zone", "name": "exit", "kind": "exit", "x": 140, "y": 60, "width": 20, "height": 28},
        {"type": "entry", "name": "start", "x": 20, "y": 87}]},
      {"name": "actors", "factor": 256, "order": "added", "objects": [
        {"type": "actor", "name": "hero", "set": "HERO", "script": "stand", "x": 20, "y": 87}]}],
    "grids": [{"name": "floor", "tileset": "bricks", "x": 0, "y": 88, "cols": 20, "rows": 2,
               "cells": [[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1],
                         [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]]}]}],
  "game": [{"name": "start_ms", "value": 500}]
}
```

Рядом лежат `HERO/stand.png`, `HERO/step.png`, `WALL/brick.png` и
`HERO.anim`, чьи кадры называются `HERO_STAND` и `HERO_STEP`, а сигнал —
именем из `constants` (`signal STRIDE`). Из этого получается заголовок с
`MYGAME_SCENE_FIELD_ACTOR_HERO`, `MYGAME_SCENE_FIELD_ZONE_EXIT`,
`MYGAME_SCENE_FIELD_GRID_FLOOR`, `MYGAME_BRICKS_TILE_BRICK`,
`MYGAME_GAME_START_MS`, `MYGAME_STRIDE` и всем остальным.

Документ, который не написан редактором, лучше всего сверять с тестами
упаковщика в репозитории движка: `test/host/test_sprites.py` пишет
документ с наборами, масками, точками, плитками, стилями, диалогом и
переводом, а `test/host/test_scenes.py` — со сценами, связями, графом и
игрой целиком; оба проверяют, что из этого получилось, байт за байтом.
Полное описание формата выйдет вместе с самим редактором. Сегодня
упаковщик читает только известные ему поля, остальные он не замечает.

### 2.3. Документ и скрипт вместе

Третий вход — скрипт, который читает документ и добавляет то, чего в
документе нет: отсчёты звуков, байты своего формата, цвета без картинок,
второй язык. После `add_sprites` пакет — это тот же `Pack`, и можно
продолжать обычными вызовами:

```python
pack = Pack("MYGAME")
pack.add_sprites(os.path.join(ROOT, "game_data", "mygame.json"))
# чего в документе нет: отсчёты звука, который он называет, цвет неба, свои байты
pack.add_sound("step", wav=os.path.join(ROOT, "sounds", "step.wav"))
pack.add_set("SKY", [(150, 205, 250)])
pack.add_blob("LEVELS", open(os.path.join(ROOT, "levels.bin"), "rb").read())
pack.write(...)
```

Со звуками так. Документ называет звуки только по именам
(`"sounds": ["step"]`): такой звук получает номер по месту в списке, но
пока ничего не играет. Скрипт даёт ему отсчёты тем же `add_sound` с тем же
именем, и номер остаётся тем, что дал документ: скрипты анимации называют
звук именно номером (`sound 0`). Звук, которого документ не называл,
получает следующий номер. Отсчёты одному звуку даются один раз; второй раз —
отказ, `sound STEP twice`.

### 2.4. Почему не писать пакет самому

Пакет — обычный двоичный файл, и написать его можно и без упаковщика:
каждый его формат версионирован и описан в заголовках движка (пакет — в
`include/dgx_ge_pack.h`, сцена — в `include/dgx_ge_layout.h` и так далее),
на плате пакет открывается проверкой магии, версии и размеров. Но тогда
теряется всё, ради чего упаковщик существует: проверка имён и ссылок, цветов
наборов, скриптов анимации и диалогов, правил переходов, графа и связей;
перевод строк; номера в заголовке, который сходится с пакетом; отпечаток
пакета, по которому сохранения узнают свои данные. Ошибку, которую упаковщик
назвал бы файлом и строкой, на плате пришлось бы искать по поведению.

Поэтому я советую упаковщик как единственную дверь. Своя программа на
другом языке может писать документ `dgx-ge-sprites` (вход 2.2) или вызывать
упаковщик как скрипт (вход 2.1) — и то и другое дешевле, чем повторять его
проверки.

### 2.5. Что упаковщик не пропустит

Упаковщик останавливается на первой ошибке и называет файл, строку и имя.
Анимацию или диалог, переданные с `path=`, он называет файлом; без него —
именем. Примеры настоящих отказов:

| Что не так | Что он говорит |
| --- | --- |
| опечатка в имени кадра скрипта | `AnimError: .../HERO.anim: line 11: no picture 'HERO_STPE'` |
| у кадра документа нет PNG | `ValueError: .../mygame.json: no picture jump.png of set HERO` |
| персонаж сцены начинает со скрипта, которого у вида нет | `ValueError: .../mygame.json: scene field: actor hero: no script run in HERO` |
| реплика роли, которой нет в диалоге | `DialogError: dialogs/hi.dlg: line 3: no role 'guard'` |
| перевод показывает число, которого в строке нет | `DialogError: dialogs/hello.dlg: hello: 'Привет, {hero}!' shows {hero}, a number the line 'Hello! I am going for a walk.' has not` |
| приставка не заглавными | `ValueError: the prefix of a pack 'mygame': capitals, digits and _, from a capital (MYGAME: MYGAME_SET_HERO)` |
| блоб с именем того, что упаковщик пишет сам | `ValueError: a blob named GAME: the packer's own blob GAME has that name (an animation, a dialog, a blob)` |
| PNG не с палитрой | `ValueError: .../rgb.png: not a picture of a palette (RGB)` |
| отсчёты одному звуку дважды | `ValueError: sound STEP twice` |
| имя числа выходит в заголовке именем другого | `ValueError: constant SET_COUNT: its name in the header, MYGAME_SET_COUNT, is another thing's: '#define MYGAME_SET_COUNT 3'; give the constant another name` |

Цвет картинки, которого нет в её наборе, — не ошибка, а предупреждение: он
заменяется ближайшим, и упаковщик это говорит
(`.../red.png: color 1 (255, 0, 0) is not in set A, taken as (250, 0, 0)`).
Два имени, которые в заголовке выходят одинаковыми (`stand-1` и `stand_1`),
— тоже отказ: иначе один макрос назвал бы две вещи.

Строгость здесь на пользу: каждый такой отказ на плате был бы невидимой
ошибкой — актёр, который стоит, диалог без реплики, номер чужой картинки.

## 3. Что выходит из упаковщика

| Файл | Чем пишется | Что это | Кто его берёт |
| --- | --- | --- | --- |
| `mygame.pack` | `write` | все данные игры в одном файле | прошивка (`EMBED_FILES`), компьютер (`GAME_EMBED`) |
| `mygame_pack.h` | `write` | номера всего по именам | код C; сборка — число параметров актёра |
| `mygame.fonts` | `write_fonts` | запросы шрифтов DGX | `dgx_ge_fonts` в прошивке, `GAME_FONTS` на компьютере |
| `mygame.letters` | `write_letters` | буквы, которые пакет может напечатать | запросы шрифтов ссылаются на него |
| `mygame.typed` | `write_fonts` | что можно набрать в полях игры | шрифт с `typed=True` |
| `mygame.pot` | `write_pot` | шаблон перевода: все строки диалогов в их контекстах | переводчик |
| `mygame.sounds` | `write(..., sounds_path=)` | отсчёты звуков отдельно от пакета | игра, если встраивает их сама |

**Пакет.** Заголовок в 52 байта (магия `DGPK`, версия, размер, таблицы),
затем таблицы наборов, картинок, блобов, масок и точек и данные, каждое с
границы в 4 байта. Последние четыре байта заголовка — отпечаток пакета,
CRC-32 всего файла (`dgx_ge_pack_id`): сохранение помнит его, и после
прошивки с другими данными старый сейв не откроется
(обзор, [2.15](overview-ru.md#215-сохранение-игры)). Картинки лежат
распакованными, 1, 4 или 8 бит на точку, строка с нового байта: так их
можно рисовать прямо из flash. Поэтому пакет не копируется в RAM:
`dgx_ge_pack_open` только проверяет его и запоминает, где таблицы, а
`dgx_ge_pack_image` и `dgx_ge_pack_blob` отдают указатели внутрь
([2.2](overview-ru.md#22-пакет-ресурсов)). На ESP32 встроенный файл идёт в
раздел данных flash, и RAM он не занимает вовсе.

**Заголовок.** Код игры обращается к данным только по его именам, и любое
переименование в данных — ошибка компиляции, а не тихий промах на плате.
Вот его начало и середина для `mygame` из раздела 2.1:

```c
/* Made by dgx_ge_pack.py: the numbers of mygame.pack. Do not edit. */
#pragma once

#define MYGAME_SET_HERO 0
#define MYGAME_SET_GROUND 1
#define MYGAME_SET_UI 2
#define MYGAME_SET_COUNT 3

#define MYGAME_IMG_HERO_STAND 0
#define MYGAME_IMG_HERO_STEP 1
#define MYGAME_IMG_GROUND 2
#define MYGAME_IMG_COUNT 3

#define MYGAME_BLOB_HERO 0
#define MYGAME_BLOB_HELLO 1
#define MYGAME_BLOB_STRINGS 2
#define MYGAME_BLOB_GAME 3
#define MYGAME_BLOB_STYLES 4
#define MYGAME_BLOB_SOUNDS 5
#define MYGAME_BLOB_COUNT 6

#define MYGAME_GAME_WALK_MS 0
...
#define MYGAME_ANIM_PARAMS 2
#include "dgx_ge_anim.h"
#if DGX_GE_ANIM_PARAMS < MYGAME_ANIM_PARAMS
#error "the actors of this pack keep 2 params, the build fewer (DGX_GE_ANIM_PARAMS): dgx_ge_params_of_pack(<this header>) in the game's CMakeLists, GAME_PACK_H in its Makefile of the computer"
#endif

#define MYGAME_HERO_SCRIPT_STAND 0
#define MYGAME_HERO_SCRIPT_WALK 1
#define MYGAME_HERO_EVENT_WALK 0
#define MYGAME_HERO_EVENT_STOP 1
#define MYGAME_HERO_PARAM_STRIDES 0

#define MYGAME_STYLE_TALK 0
#define MYGAME_FONT_TEXT 0
#define MYGAME_FONTS(X) X(TEXT, text)
#define MYGAME_FONT_COUNT 1
```

Имена получаются из имён данных: буквы и цифры остаются, всё прочее
становится `_`, буквы — заглавными. Семьи имён:

| Имена | Что это |
| --- | --- |
| `<P>_SET_*`, `<P>_IMG_*`, `<P>_BLOB_*` и их `_COUNT` | наборы цветов, картинки, блобы (анимации, диалоги, сцены, плитки, графы, свои байты и то, что упаковщик пишет сам: `STRINGS`, `GAME`, `TIMERS`, `STYLES`, `CHASE`, `SOUNDS`) |
| `<P>_<ВИД>_SCRIPT_*`, `_EVENT_*`, `_PARAM_*`, `_GAME_*` | скрипты, события, параметры вида и вопросы его скриптов к игре (`game.<имя>`) |
| `<P>_EVENT_*` | события пакета: одно имя — один номер для всех видов |
| `<P>_ANIM_PARAMS` | сколько параметров должен хранить каждый актёр (ниже) |
| `<P>_<НАБОР>_TILE_*`, `_SLOT_*` | плитки набора (с 1; 0 — пусто) и его слоты |
| `<P>_SCENE_<СЦЕНА>_LAYER_*`, `_PICTURE_*`, `_ACTOR_*`, `_ZONE_*`, `_ENTRY_*`, `_GRID_*` | объекты сцены документа |
| `<P>_ZONE_*`, `<P>_SCREEN_WIDTH`, `_HEIGHT` | слова видов зон; экран игры из документа |
| `<P>_<ГРАФ>_EXIT_*`, `_ENTRY_*`, `_MAP_*` | выходы, входы и карты графа |
| `<P>_GAME_*` | места констант игры в блобе `GAME` |
| `<P>_<ИМЯ>` | имена чисел, которые пишут скрипты (`add_constants`, `constants` документа), с самим числом |
| `<P>_MASK_*`, `<P>_POINT_*` | маски и точки привязки |
| `<P>_SOUND_*`, `<P>_TIMER_*` | звуки и таймеры по номерам |
| `<P>_STYLE_*`, `<P>_FONT_*`, `<P>_FONTS(X)`, `<P>_FONT_COUNT` | стили, шрифты игры и их таблица по именам |
| `<P>_FIELD_*`, `<P>_<ДИАЛОГ>_ROLE_*` | поля ввода и роли диалогов |

**Параметры актёра.** Каждый актёр хранит у себя числа — параметры — в
массиве, размер которого задаётся при сборке движка
(`DGX_GE_ANIM_PARAMS`, по умолчанию 4). Пакет знает, сколько их нужно
самому жадному из его видов, и пишет это в `<P>_ANIM_PARAMS` (не меньше
двух: жизни клеток держат в первых двух модификатор и плитку). Сборка
должна передать это число всей прошивке — и движку, и игре, иначе у них
будут разные `dgx_ge_actor_t`. Для платы это делает
`dgx_ge_params_of_pack`, для компьютера — `GAME_PACK_H` (раздел 6). Если
это забыто, а виду нужно больше параметров, чем движок хранит по умолчанию,
компиляцию останавливает `#error` из самого заголовка. Анимация, которой
нужно больше параметров, чем хранит сборка, на плате просто не откроется.

**Шрифты, буквы, шаблон перевода.** Игра не носит с собой готовых шрифтов:
она говорит, какой шрифт нужен (`add_font("text", points=10)` — «10 точек
кадра»), а DGX делает его при сборке, для дисплея и масштаба платы, только
с теми буквами, которые пакет может напечатать
([2.12](overview-ru.md#212-текст-надписи-диалоги-шрифты)). Для `mygame`:

```
$ cat main/assets/mygame.fonts
# The fonts of the game, as requests of DGX's fonts: made by dgx_ge_pack.py (Pack.add_font), not to be edited.
need text
size 10pt
text mygame.letters
$ cat main/assets/mygame.letters
 !.HIaefgiklmnorw
$ cat main/assets/mygame.pot
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"

msgctxt "hello"
msgid "Hero"
msgstr ""

msgctxt "hello"
msgid "Hello! I am going for a walk."
msgstr ""
```

**Звуки отдельно.** `write(pack, header, sounds_path=...)` кладёт отсчёты
звуков не в пакет, а в свой файл: плата без звука не тратит на них flash.
Имена и номера звуков остаются в заголовке, а `<P>_BLOB_SOUNDS` пропадает;
такой файл игра встраивает и открывает сама (`dgx_ge_sounds_open`).

## 4. Пакет заново сам

Запускать упаковщик руками после каждой правки рисунка скоро надоедает, да
и легко забыть. Удобнее, когда это делают сами сборки: перед тем как
собирать игру, они смотрят, не новее ли данные пакета, и, если новее,
собирают его заново.

Упаковщик при этом зовут раньше всего остального, а не обычным правилом
сборки. Обе сборки читают заголовок пакета ещё до того, как что-то
компилируют: `dgx_ge_params_of_pack` — при настройке CMake, `host.mk` —
когда make читает его самого. Число параметров, прочитанное там, уходит в
команду компилятора для всех файлов. Пакет, собранный позже, обычным
правилом, пришёл бы к сборке с опозданием: когда число параметров выросло,
она взяла бы его из старого заголовка и остановилась бы на `#error`. Поэтому
прошивка собирает пакет при настройке CMake, а `host.mk` — пока его читают,
до того как он берёт число параметров из заголовка.

**Прошивка.** В `main/CMakeLists.txt` скрипт пакета запускается при
настройке сборки (`execute_process`), а файлы данных перечисляются в
`CMAKE_CONFIGURE_DEPENDS`: правка любого из них заставляет `idf.py build`
настроить сборку заново, а значит, и пересобрать пакет.

```cmake
# main/CMakeLists.txt
idf_build_get_property(GAME_BOARD GAME_BOARD)
idf_build_get_property(engine GAME_ENGINE)
include(${engine}/boards/${GAME_BOARD}/board.cmake)

# пакет — при настройке сборки; правка данных настраивает её заново
set(game ${CMAKE_CURRENT_LIST_DIR}/..)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
	find_program(GAME_PYTHON NAMES python3 PATHS /usr/bin /usr/local/bin /opt/homebrew/bin NO_DEFAULT_PATH)
	if(NOT GAME_PYTHON)
		find_program(GAME_PYTHON NAMES python3 python REQUIRED)
	endif()
	file(GLOB inputs ${game}/art/*.png ${game}/anim/*.anim ${game}/dialogs/*.dlg ${game}/po/*.po)
	execute_process(COMMAND ${GAME_PYTHON} ${game}/tools/make_pack.py ${engine} RESULT_VARIABLE packed)
	if(NOT packed EQUAL 0)
		message(FATAL_ERROR "the pack could not be made (${GAME_PYTHON} tools/make_pack.py: see above)")
	endif()
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs} ${game}/tools/make_pack.py)
endif()

idf_component_register(
	SRCS "main.c" ${BOARD_SRCS}
	INCLUDE_DIRS "." ${BOARD_INCLUDE_DIRS}
	REQUIRES jef-sure__dgx-ge jef-sure__dgx
	PRIV_REQUIRES esp_timer ${BOARD_REQUIRES}
	EMBED_FILES "assets/mygame.pack"
)
dgx_ge_params_of_pack(assets/mygame_pack.h)
dgx_ge_fonts(assets/mygame.fonts)
dgx_fonts_board(PIXELS ${BOARD_PIXELS} MM ${BOARD_MM})
dgx_fonts_scale(2)
dgx_fonts_rules(../fonts.rules)
```

Что здесь зачем:

- `CMAKE_BUILD_EARLY_EXPANSION`. ESP-IDF читает `CMakeLists.txt`
  компонентов дважды: сначала отдельным скриптом, только чтобы узнать их
  зависимости, потом по-настоящему. Упаковщик нужен один раз, во втором
  чтении. Файл платы (`board.cmake`) включается в обоих: в первом он
  говорит, что плате нужно (`BOARD_REQUIRES`).
- Свойства сборки `GAME_BOARD` и `GAME_ENGINE` задаёт `CMakeLists.txt`
  проекта (раздел 6). Обычные переменные проекта в первое чтение не
  доходят, а свойства сборки ESP-IDF передаёт и туда.
- `python3` — системный, с Pillow, а не тот, что активирует ESP-IDF: в его
  окружении Pillow нет. `find_program` с `NO_DEFAULT_PATH` сначала ищет в
  системных каталогах и только потом где угодно.
- Файлы данных перечисляются явно (`file(GLOB ...)`): новый файл,
  которого не было при прошлой настройке, сборка сама не заметит — после
  него нужна `idf.py reconfigure`.
- `EMBED_FILES` следит за самим пакетом: изменился — встраивается заново.
- `dgx_ge_params_of_pack` читает уже свежий заголовок, раз пакет собран до
  него.

Так это выглядит, когда в `anim/hero.anim` добавлены два параметра (их
стало три) и запущена `idf.py build`:

```
[0/1] Re-running CMake...
...
mygame.pack: 1180 bytes
```

и в той же сборке все 959 команд компиляции прошивки (движок, DGX,
ESP-IDF, игра) получают `DGX_GE_ANIM_PARAMS=3`.

**Компьютер.** Здесь это делает сам `host.mk`; `Makefile` игры только
говорит ему две вещи: чем делается пакет (`GAME_PACK_MAKE`) и из чего
(`GAME_PACK_INPUTS`).

```make
# пакет заново, когда данные или скрипт новее заголовка: host.mk делает его, пока его читают
GAME_PACK_MAKE   := python3 tools/make_pack.py $(ENGINE)
GAME_PACK_INPUTS := tools/make_pack.py $(wildcard art/*.png anim/*.anim dialogs/*.dlg po/*.po)

include $(ENGINE)/boards/host/host.mk
```

Если заголовка `GAME_PACK_H` нет или хоть один файл из `GAME_PACK_INPUTS`
новее его, `host.mk` запускает команду сразу, пока make его читает, и
печатает `the pack: <команда>`; число параметров он берёт уже из нового
заголовка. Команда не удалась — сборка останавливается:
`the pack was not made: <команда> failed (it says why above)`, а выше —
сообщение самого упаковщика. `make clean` пакет не делает. Так же сделан
`Makefile` примера `hello`. Вот первый `make` после того, как в
`anim/hero.anim` добавлены два параметра:

```
the pack: python3 tools/make_pack.py <движок>
mygame.pack: 1180 bytes
...
```

и вся программа собирается с `DGX_GE_ANIM_PARAMS=3` за этот же запуск.

## 5. Сторона C

Код игры открывает пакет, настраивает плату и кадр, раскладывает сцену,
заводит актёров и крутит цикл. Всё, что игра знает о данных, она берёт из
заголовка по именам. Ниже — сокращённая игра `mygame` из раздела 2.1:
герой говорит реплику, после неё гуляет столько, сколько сказано в числе
игры `walk_ms`.

```c
#include "assets/mygame_pack.h"
#include "dgx_fonts.h"          /* шрифты, которые DGX сделал по запросам пакета */
#include "dgx_ge_anim.h"
#include "dgx_ge_board.h"
#include "dgx_ge_dialog.h"
#include "dgx_ge_frame.h"
#include "dgx_ge_game.h"
#include "dgx_ge_input.h"
#include "dgx_ge_mixer.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_scene.h"
#include "dgx_ge_talk_view.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* пакет, встроенный сборкой: имена символов — от имени файла */
extern const uint8_t mygame_pack_start[] asm("_binary_mygame_pack_start");
extern const uint8_t mygame_pack_end[] asm("_binary_mygame_pack_end");

enum { FRAME_W = 160, FRAME_H = 100, GROUND_Y = FRAME_H - 4 };
enum { LAYER_BACK, LAYER_ACTORS, LAYER_TALK, LAYERS };

static dgx_ge_pack_t pack;
static uint8_t       maps[MYGAME_SET_COUNT][16];   /* цвета каждого набора: номера в кадре */
static dgx_ge_kinds_t kinds;
static dgx_ge_actor_t hero;
...

/* что скрипт говорит игре: звук, сигнал */
static void say(dgx_ge_actor_t *actor, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what == DGX_GE_ANIM_SOUND) dgx_ge_sound_play(&sound, value);
    if (what == DGX_GE_ANIM_SIGNAL && value == 1)
        ESP_LOGI("mygame", "a stride, %ld so far", (long)actor->params[MYGAME_HERO_PARAM_STRIDES]);
}

void app_main(void)
{
    board_t board;
    if (!board_display_init(&board)) return;                               /* дисплей платы */
    if (!dgx_ge_pack_open(&pack, mygame_pack_start, mygame_pack_end - mygame_pack_start)) return;

    /* кадр игры так крупно, как позволяет дисплей: x2 на CYD */
    dgx_screen_t *lcd = board.lcd;
    int scale = lcd->width / FRAME_W < lcd->height / FRAME_H ? lcd->width / FRAME_W : lcd->height / FRAME_H;
    scale = scale < 1 ? 1 : scale;
    dgx_ge_frame_t *frame = dgx_ge_frame_create(lcd, (lcd->width - FRAME_W * scale) / 2,
                                                (lcd->height - FRAME_H * scale) / 2, FRAME_W, FRAME_H);
    if (frame && scale > 1 && !dgx_ge_frame_scale(frame, scale)) frame = NULL;
    if (frame && board_band) dgx_ge_frame_band(frame, board_band);
    dgx_ge_scene_t *scene = frame ? dgx_ge_scene_create(frame, LAYERS, 4, 4) : NULL;
    if (!scene) return;
    dgx_ge_scene_layer_pixels(scene, LAYER_TALK, true);                     /* разговор — в пикселях дисплея */

    /* цвета всех наборов кадру; бумага (номер 0) — небо */
    for (int set = 0; set < MYGAME_SET_COUNT; ++set) {
        const uint8_t (*rgb)[3];
        const int n = dgx_ge_pack_set_colors(&pack, set, &rgb);
        dgx_ge_frame_add_colors(frame, rgb, n, maps[set]);
        if (set == MYGAME_SET_UI) dgx_ge_frame_set_color(frame, 0, rgb[0][0], rgb[0][1], rgb[0][2]);
    }
    dgx_ge_scene_reset(scene, 0);
    /* декорации: земля, один раз */
    ...

    /* числа игры, виды, герой */
    size_t size;
    dgx_ge_game_t game;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, MYGAME_BLOB_GAME, &size);
    dgx_ge_game_open(&game, blob, size);
    const uint32_t walk_ms = dgx_ge_game_value(&game, MYGAME_GAME_WALK_MS, 1000);
    dgx_ge_kinds_init(&kinds, &pack, 4);
    uint32_t now = esp_timer_get_time() / 1000;
    dgx_ge_actor_start(&hero, dgx_ge_kind(&kinds, MYGAME_BLOB_HERO), MYGAME_HERO_SCRIPT_STAND, 20, GROUND_Y - 1, now);
    /* звук — как в hello: dgx_ge_sounds_open на блобе SOUNDS, dgx_ge_mixer_init, board_sound_start */
    ...

    /* диалог: строки, стили, шрифты игры по именам заголовка */
    static dgx_font_t *fonts[MYGAME_FONT_COUNT];
    static bool        made[MYGAME_FONT_COUNT];
#define FONT_OF(N, n) fonts[MYGAME_FONT_##N] = dgx_font_##n(), made[MYGAME_FONT_##N] = DGX_FONT_##N##_MADE && frame->scale == DGX_FONTS_SCALE;
    MYGAME_FONTS(FONT_OF)
#undef FONT_OF
    blob = dgx_ge_pack_blob(&pack, MYGAME_BLOB_STRINGS, &size);
    dgx_ge_strings_open(&strings, blob, size);
    blob = dgx_ge_pack_blob(&pack, MYGAME_BLOB_HELLO, &size);
    dgx_ge_dialog_open(&hello, blob, size);
    blob = dgx_ge_pack_blob(&pack, MYGAME_BLOB_STYLES, &size);
    dgx_ge_talk_view_init(&view, blob, blob ? size : 0, frame, fonts, MYGAME_FONT_COUNT);
    dgx_ge_talk_view_pixels(&view, true, made);
    dgx_ge_actor_t *const cast[1] = {&hero};
    dgx_ge_talk_start(&talk, &hello, &(dgx_ge_cast_t){.roles = cast}, MYGAME_STYLE_TALK, now, say, NULL);

    dgx_ge_input_t input = {0};
    uint32_t walk_to = 0;
    for (;;) {
        const int64_t t0 = esp_timer_get_time();
        now = t0 / 1000;
        dgx_ge_input_update(&input, board_buttons());                      /* ввод */

        dgx_ge_actor_advance(&hero, now, say, NULL);                        /* шаги, чьё время пришло */
        if (talk.state != DGX_GE_TALK_OVER) {                               /* события: кнопки — разговору */
            if (input.pressed & (DGX_GE_KEY_A | DGX_GE_KEY_B)) dgx_ge_talk_view_event(&view, &talk, DGX_GE_UI_ACTIVATE, now);
            dgx_ge_talk_view_play(&view, &talk, &strings, now);
            if (talk.state == DGX_GE_TALK_OVER) walk_to = now + walk_ms;
        } else if (walk_to) {                                               /* или герою */
            dgx_ge_actor_event(&hero, (int32_t)(now - walk_to) < 0 ? MYGAME_HERO_EVENT_WALK : MYGAME_HERO_EVENT_STOP, now, say, NULL);
        }

        dgx_ge_scene_actors_begin(scene);                                   /* что видно в этом кадре */
        static dgx_image_t picture;                                         /* герой: его картинка сейчас */
        dgx_ge_item_t it = {.x = dgx_ge_actor_x(&hero) + hero.image_dx, .y = dgx_ge_actor_y(&hero) + hero.image_dy,
                            .layer = LAYER_ACTORS};
        if (hero.image >= 0 && dgx_ge_pack_image(&pack, hero.image, &picture)) {
            it.image = &picture;
            it.map   = maps[dgx_ge_pack_image_set(&pack, hero.image)];
            dgx_ge_scene_actor(scene, &it);
        }
        dgx_ge_talk_view_show(&view, &talk, scene, LAYER_TALK);
        dgx_ge_scene_render(scene);                                         /* нарисовать изменённое и отправить */

        const int64_t spent = esp_timer_get_time() - t0;                    /* шаг цикла не чаще 20 мс */
        vTaskDelay(spent < 20000 ? pdMS_TO_TICKS((20000 - spent) / 1000) + 1 : 1);
    }
}
```

По частям, что здесь происходит и где об этом подробнее:

| Шаг | Что делает | Где подробнее |
| --- | --- | --- |
| пакет | `dgx_ge_pack_open` по символам, которые дала сборка; ничего не копирует | обзор, [2.2](overview-ru.md#22-пакет-ресурсов) |
| плата | `board_display_init` из `boards/dgx_ge_board.h`: дисплей; там же касание (`board_pointer_*`), кнопки (`board_buttons`), геймпад и клавиатура на дисплее, звук (`board_sound_start`) | [2.17](overview-ru.md#217-плата), [host-ru.md](host-ru.md#1-как-это-устроено) |
| кадр | `dgx_ge_frame_create`, `dgx_ge_frame_scale`; цвета наборов — `dgx_ge_frame_add_colors` | [2.3](overview-ru.md#23-картинки-и-цвета), [2.4](overview-ru.md#24-кадр) |
| сцена | `dgx_ge_scene_create` (слои, декорации, актёры), декорации между `dgx_ge_scene_set_begin` и `_end` | [2.5](overview-ru.md#25-сцена) |
| виды и актёры | `dgx_ge_kinds_init` и `dgx_ge_kind(&kinds, <P>_BLOB_<ВИД>)` — анимация вида; `dgx_ge_actor_start` со скриптом из заголовка | [2.7](overview-ru.md#27-актёры-и-скрипты) |
| цикл | ввод (`dgx_ge_input_update`), `dgx_ge_actor_advance`, решения и события (`dgx_ge_actor_event`), что видно (`dgx_ge_scene_actors_begin`, `dgx_ge_scene_actor`), `dgx_ge_scene_render`, `vTaskDelay` | [2.18](overview-ru.md#218-цикл-игры-целиком) |
| `say` | что скрипт говорит игре: `signal`, `sound`, таймеры, клетки, появление | [animation-ru.md](animation-ru.md#5-что-скрипт-сообщает-игре) |
| числа игры | `dgx_ge_game_open` на блобе `GAME`, `dgx_ge_game_value(&game, <P>_GAME_<ИМЯ>, иначе)` | `include/dgx_ge_game.h` |
| диалоги | `dgx_ge_strings_open`, `dgx_ge_dialog_open`, `dgx_ge_talk_start`; показ — `dgx_ge_talk_view_*` | [2.12](overview-ru.md#212-текст-надписи-диалоги-шрифты) |

Остальное подключается так же — блоб из пакета по имени заголовка и
открытие:

- **Сцена из документа** (вход 2.2): `dgx_ge_layout_open` на блобе
  `<P>_BLOB_SCENE_<СЦЕНА>`; слои и картинки — `dgx_ge_layout_scene`, сетка —
  `dgx_ge_layout_grid`, `dgx_ge_tileset_open` и `dgx_ge_layout_grid_set`,
  потом `dgx_ge_grid_add`; персонаж — `dgx_ge_layout_actor_start(&layout,
  <P>_SCENE_<СЦЕНА>_ACTOR_<ИМЯ>, &actor, &kinds, now)`; зона —
  `dgx_ge_layout_zone` ([«Сцена из редактора»](overview-ru.md#сцена-из-редактора)):

  ```c
  dgx_ge_layout_grid_t lg;
  dgx_ge_layout_grid(&layout, MYGAME_SCENE_FIELD_GRID_FLOOR, &lg);
  const uint8_t *ts = dgx_ge_pack_blob(&pack, lg.tiles, &size);
  dgx_ge_tileset_open(&tiles, &pack, ts, size);
  for (int i = 0; i < lg.cols * lg.rows; ++i) cells[i] = lg.cells[i];   /* клетки — копия игры */
  dgx_ge_layout_grid_set(&lg, &grid, &tiles, cells);
  grid.maps = map_of;
  dgx_ge_scene_set_begin(scene);
  dgx_ge_layout_scene(&layout, scene, &pack, map_of, pictures);
  dgx_ge_grid_add(&grid, scene);
  dgx_ge_scene_set_end(scene);
  dgx_ge_layout_actor_start(&layout, MYGAME_SCENE_FIELD_ACTOR_HERO, &hero, &kinds, now);
  dgx_ge_layout_zone(&layout, MYGAME_SCENE_FIELD_ZONE_EXIT, &exit_zone, &kind);
  ```

- **Мир, которого не видно**: что лежит в сценах, на которые никто не
  смотрит, хранится записями, а не актёрами (`dgx_ge_world.h`). Уходя из
  сцены, игра делает из актёра запись (`dgx_ge_object_of`,
  `dgx_ge_world_add`); показывая сцену снова, перебирает её записи
  (`dgx_ge_world_next`, `dgx_ge_world_get`) и делает из них актёров
  (`dgx_ge_object_actor`), которых `dgx_ge_actor_advance` догоняет до
  настоящего момента ([2.10](overview-ru.md#210-живой-мир)).
- **Таймеры**: `dgx_ge_timers_init`, `dgx_ge_timers_define` на блобе
  `TIMERS`; граф сцен — `dgx_ge_graph_open`; заставки — `dgx_ge_show.h`.
- **Сохранение**: `dgx_ge_save_begin` с `dgx_ge_pack_id(&pack)`, куски
  частей движка (`dgx_ge_world_save` и другие) и свои, `dgx_ge_save_end`;
  куда положить байты (NVS, файл), решает игра
  ([2.15](overview-ru.md#215-сохранение-игры)). `examples/hello` хранит
  свой счёт в NVS.

**Что остаётся на C, а что нет.** Если что-то можно сказать данными, это
стоит сказать данными: тогда правка не требует перекомпиляции, упаковщик
проверяет её до платы, а движок исполняет одинаково везде.

| Данными | На C |
| --- | --- |
| что делает персонаж, сколько длится каждый кадр, куда он движется, какие звуки и сигналы — скрипт `.anim` | какие кнопки становятся какими событиями |
| что из чего следует (матрица переходов `on ...`) | решения, которым нужно знание всей игры: куда целиться, за кем гнаться, что считать собранным рядом; ответы на вопросы скриптов (`game.<имя>`, `dgx_ge_expr_ask`) |
| числа игры: скорости, жизни, размеры — `add_game`; имена чисел для скриптов — `add_constants` | настройка платы и кадра, масштаб |
| уровень: сцена, сетка, плитки с правилами | где хранить сохранение |
| таймеры, заставки, погони, толпы, диалоги, стили | главный цикл |

Хороший признак того, что код взял на себя лишнее: в C появились числа,
которые хочется подбирать (скорость, задержка, сколько очков за что). Их
место — в данных, а C читает их по имени из заголовка.

## 6. Сборка

### 6.1. Для платы

Проект ESP-IDF игры, которая собирается и для платы, и для компьютера:

```
mygame/
├── CMakeLists.txt            проект ESP-IDF: выбор платы, где движок
├── sdkconfig.defaults        своя часть настроек игры
├── boards/cyd/               настройки платы, скопированные из движка
│   └── sdkconfig.defaults
├── Makefile                  сборка для компьютера
├── fonts.rules               правила выбора шрифтов (если игра их заказывает)
├── tools/make_pack.py        скрипт пакета
├── art/  anim/  dialogs/  po/   данные (или game_data/ с документом редактора)
└── main/
    ├── CMakeLists.txt        пакет, плата, EMBED_FILES, параметры, шрифты (раздел 4)
    ├── idf_component.yml     движок и что нужно платам
    ├── main.c
    └── assets/               mygame.pack, mygame_pack.h и остальное, что пишет упаковщик
```

Шаги:

1. **Движок в зависимости.** `idf.py add-dependency "jef-sure/dgx-ge^0.3.0"`
   добавляет его в `main/idf_component.yml`, DGX приходит с ним сам. DGX-DI
   (указатель, геймпад, клавиатура на дисплее) нужен платам с тачем и
   сборке для компьютера: `idf.py add-dependency "jef-sure/dgx-di^0.1.2"`.
   Плата компьютера и часть того, о чём здесь речь, появились в движке
   в версии 0.2.0. Чтобы взять движок из клона репозитория, в
   `main/idf_component.yml` ему дают `override_path`:

   ```yaml
   dependencies:
     jef-sure/dgx-ge:
       version: "*"
       override_path: "../../esp32-component-dgx-ge"   # путь от main/ к клону
     jef-sure/dgx-di: "^0.1.2"
   ```

   Тогда движок лежит не в `managed_components/`, и обеим сборкам говорят,
   где он: `idf.py -D GAME_ENGINE=<клон>` (или значение `GAME_ENGINE` в
   `CMakeLists.txt` проекта, шаг 3) и `make ENGINE=<клон>`. DGX и DGX-DI
   менеджер компонентов всё равно кладёт в `managed_components/`, и
   `host.mk` находит их там сам.

   Платам с ESP32-P4 нужны ещё зависимости, которые называет их
   `board.cmake`: звук ES8311 (`espressif/esp_codec_dev`) и, у 7″, тач GT911
   (`espressif/esp_lcd_touch_gt911`), с правилом `if: "target == esp32p4"`,
   чтобы CYD их не тянул. Готовый список — в
   `examples/hello/main/idf_component.yml`.

2. **Настройки платы.** У каждой платы движка в `boards/<плата>/` лежат
   `sdkconfig.defaults` и, у ESP32-P4, файл ревизии чипа: они включают
   нужные DGX шину и драйвер дисплея, виртуальные экраны и PSRAM
   ([2.17](overview-ru.md#217-плата)). Их копируют в проект, в
   `boards/<плата>/`: ESP-IDF проверяет файлы `SDKCONFIG_DEFAULTS` раньше,
   чем скачивает компоненты в `managed_components/`, и в свежем проекте не
   нашёл бы их там.
3. **`CMakeLists.txt` проекта** выбирает плату и говорит, где движок. Плата
   — свойство сборки, а не переменная: так её видит и первое чтение
   `main/CMakeLists.txt` (раздел 4).

   ```cmake
   cmake_minimum_required(VERSION 3.22)

   set(GAME_BOARD cyd CACHE STRING "the board: cyd, jc8012p4a1c (10\"), jc1060p470 (7\")")
   # где движок: там, куда его кладёт менеджер компонентов, если не сказано иначе (override_path)
   set(GAME_ENGINE ${CMAKE_CURRENT_LIST_DIR}/managed_components/jef-sure__dgx-ge CACHE PATH "the engine's directory")
   set(board ${CMAKE_CURRENT_LIST_DIR}/boards/${GAME_BOARD})
   if(GAME_BOARD STREQUAL "cyd")
   	set(board_target esp32)
   	set(board_defaults "${board}/sdkconfig.defaults")
   elseif(GAME_BOARD STREQUAL "jc8012p4a1c")
   	set(board_target esp32p4)
   	set(board_defaults "${board}/sdkconfig.defaults;${board}/sdkconfig.defaults.rev3")
   elseif(GAME_BOARD STREQUAL "jc1060p470")
   	set(board_target esp32p4)
   	set(board_defaults "${board}/sdkconfig.defaults;${board}/sdkconfig.defaults.rev1")
   else()
   	message(FATAL_ERROR "GAME_BOARD ${GAME_BOARD}: cyd, jc8012p4a1c or jc1060p470")
   endif()
   if(NOT SDKCONFIG_DEFAULTS)
   	set(SDKCONFIG_DEFAULTS "${board_defaults};sdkconfig.defaults")   # плата, ревизия, потом своё
   endif()
   if(NOT IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
   	set(IDF_TARGET ${board_target})
   endif()

   include($ENV{IDF_PATH}/tools/cmake/project.cmake)
   # замок компонентов на каждый чип: CYD (esp32) и платы P4 (esp32p4) не переписывают его друг другу
   idf_build_set_property(DEPENDENCIES_LOCK ${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.${IDF_TARGET})
   idf_build_set_property(GAME_BOARD ${GAME_BOARD})
   idf_build_set_property(GAME_ENGINE ${GAME_ENGINE})
   project(mygame)
   ```

   Свой `sdkconfig.defaults` игры идёт последним и пересиливает файлы
   платы. У `hello` в нём `CONFIG_FREERTOS_HZ=1000` (цикл спит
   миллисекундами) и `CONFIG_COMPILER_OPTIMIZATION_PERF=y`.
4. **`main/CMakeLists.txt`** — как в разделе 4: файл платы `board.cmake`
   даёт `BOARD_SRCS` (файл сборки `dgx_ge_board_<плата>.c`, который
   включает файл платы из движка: так `main` собирает его, не беря к себе
   чужой исходник), `BOARD_REQUIRES` и размеры дисплея `BOARD_PIXELS`,
   `BOARD_MM`. Заголовок плат `dgx_ge_board.h` приходит с самим движком, и
   `BOARD_INCLUDE_DIRS` теперь пуст: он оставлен, чтобы игры, которые его
   называют, собирались как прежде; пакет встраивается `EMBED_FILES`;
   `dgx_ge_params_of_pack` передаёт число параметров всей сборке;
   `dgx_ge_fonts` отдаёт запросы шрифтов DGX, а `dgx_fonts_board`,
   `dgx_fonts_scale` и `dgx_fonts_rules` говорят ему, для какого дисплея,
   масштаба и по каким правилам их делать. Масштаб шрифтов должен совпадать
   с тем, во сколько раз игра увеличит кадр на этой плате: тогда шрифт
   нарисуется как сделан (`DGX_FONT_<ИМЯ>_MADE`), а не увеличенным.
5. **Сборка и прошивка:**

   ```sh
   idf.py build flash monitor                                                       # CYD
   idf.py -B build-p4 -D SDKCONFIG=build-p4/sdkconfig -D GAME_BOARD=jc8012p4a1c build flash
   idf.py -B build-p7 -D SDKCONFIG=build-p7/sdkconfig -D GAME_BOARD=jc1060p470 build flash
   ```

   Каждая плата собирается в свой каталог со своим `sdkconfig`, поэтому
   переход между ними ничего не пересобирает. Для 10″ нужна ещё прошивка
   его тач-панели, которую нельзя раздавать: игра встраивает свой файл
   `main/jc8012p4a1c_touch.fw` (`EMBED_FILES` для этой платы, как в
   `examples/hello/main/CMakeLists.txt`), а как его сделать, сказано в
   [README примера](../examples/hello/README.md).

Сборка для CYD у `mygame` кончается так:

```
mygame.bin binary size 0x44140 bytes. Smallest app partition is 0x100000 bytes. 0xbbec0 bytes (73%) free.
```

### 6.2. Для компьютера

Всё о сборке на компьютере — что поставить, откуда взять DGX и DGX-DI, что
делает `host.mk`, как компьютер изображает плату — в
[host-ru.md](host-ru.md). Главное: рядом с `CMakeLists.txt` лежит
`Makefile`, который перечисляет то же, что CMake, и подключает
`boards/host/host.mk` движка.

```make
ENGINE          ?= managed_components/jef-sure__dgx-ge
GAME            := mygame
GAME_SRCS       := main/main.c
GAME_INCLUDES   := main
GAME_EMBED      := main/assets/mygame.pack          # как EMBED_FILES
GAME_PACK_H     := main/assets/mygame_pack.h        # как dgx_ge_params_of_pack
GAME_FONTS      := main/assets/mygame.fonts         # как dgx_ge_fonts
GAME_FONT_RULES := fonts.rules                      # как dgx_fonts_rules
GAME_FONT_SCALE := 2                                # как dgx_fonts_scale
HOST_DISPLAY    := 320x240                          # дисплей изображаемой платы: CYD
HOST_MM         := 58x43

GAME_PACK_MAKE   := python3 tools/make_pack.py $(ENGINE)   # пакет заново (раздел 4)
GAME_PACK_INPUTS := tools/make_pack.py $(wildcard art/*.png anim/*.anim dialogs/*.dlg po/*.po)

include $(ENGINE)/boards/host/host.mk
```

`make` собирает `build-host/mygame`, `make run` запускает. Шрифты делает тот
же `dgx_fonts.py` DGX, что и при сборке для платы, под изображаемый дисплей:

```
text: DejaVu Sans Book 20 px (10 pt x 2), 17 glyphs
the fonts used and their licenses: .../build-host/dgx_fonts/fonts-used.txt; make sure you may use every one of them
dgx_fonts.c and dgx_fonts.h written
```

Сборку для компьютера запускают в терминале, где окружение ESP-IDF **не**
активировано: оно ставит первым в `PATH` свой `ld`, и `host.mk`
останавливается, называя его ([host-ru.md, раздел 12](host-ru.md#12-если-что-то-пошло-не-так)).

## 7. Цикл работы

Работа над игрой идёт по кругу, и большая его часть обходится без платы.

1. **Правка данных.** Рисунок, скрипт, диалог, число игры, документ
   редактора — что угодно из раздела 1.
2. **Пакет.** Сборки делают его сами (раздел 4). Ошибка упаковщика видна
   сразу, с файлом и строкой (раздел 2.5).
3. **Компьютер.** `make run` — посмотреть и поиграть. Без человека — для
   проверки перед коммитом или для картинки — игру прогоняют драйверами SDL
   `dummy` со сценарием ввода и снимком дисплея
   ([host-ru.md, раздел 9](host-ru.md#9-без-человека-проверка-и-картинка)):

   ```sh
   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
   DGX_GE_HOST_INPUT="1200 a+, 1250 a-, 1500 a+, 1550 a-" \
   DGX_GE_HOST_QUIT_MS=2600 DGX_GE_HOST_SHOT=talk.ppm ./build-host/mygame
   ```

   ```
   I host: a display of 320x240 pixels, 58x43 mm, in a window of 640x480
   I mygame: a frame of 160x100, x2; walk_ms 3000; the font made for this scale
   I mygame: the talk is over: a walk
   I mygame: a stride, 1 so far, x 24
   I mygame: a stride, 2 so far, x 28
   I mygame: a stride, 3 so far, x 32
   I mygame: a stride, 4 so far, x 36
   I mygame: a stride, 5 so far, x 40
   I host: the display written to talk.ppm
   ```

   Первое нажатие A показывает реплику целиком (она появлялась по буквам),
   второе закрывает разговор, и герой идёт. Время в игре настоящее, поэтому
   надёжнее проверять состояние (лог, NVS), чем сравнивать снимки бит в бит.
4. **Скрипты словами.** Когда непонятно, что делает анимация,
   `tools/dgx_ge_explain.py` пересказывает её
   ([«Скрипты человеческим языком»](overview-ru.md#скрипты-человеческим-языком)):

   ```
   $ python3 tools/dgx_ge_explain.py --lang ru anim/hero.anim
   ## HERO
   **Параметры:** `strides` — в начале 0

   ### Состояния

   - **stand**: 1 кадр HERO_STAND, 1000 мс; по кругу.
   - **walk**: «stride»: звук 0; 2 кадра HERO_STEP, HERO_STAND по 120 мс, шаг 2 вперёд; шаг 2 вперёд; strides += 1; сигнал игре 1; здесь можно прервать; повторяет с «stride».

   ### Переходы

   - `walk`: stand → walk, сразу
   - `stop`: walk → stand, в точке ветвления
   ```

   Несколько файлов сразу — и он скажет ещё, кто какое событие шлёт и кто на
   него отвечает. Как его звать, печатает `--help`.
5. **Плата.** `idf.py build flash monitor`. То, чего компьютер не покажет
   ([host-ru.md, раздел 10](host-ru.md#10-чего-компьютер-не-покажет)),
   смотрят здесь: сколько RAM свободно (`idf.py size` — статическая память;
   кадр, сцена и актёры берутся из кучи при старте, поэтому полезно
   печатать `heap_caps_get_free_size` после настройки игры) и сколько
   стоит кадр (`esp_timer_get_time` вокруг `dgx_ge_scene_render`).

**Перевод.** `write_pot` даёт шаблон со всеми строками диалогов в их
контекстах (`msgctxt` — имя диалога). Он начинается заголовком с
кодировкой UTF-8, поэтому `msginit -i main/assets/mygame.pot -l ru -o
po/ru.po` сразу делает перевод в UTF-8; можно и любым другим инструментом
gettext или руками. Перевод может переставлять числа строки (`{volume}`),
но не может показать имя, которого в строке нет: такой перевод упаковщик
не пропустит (раздел 2.5). Скрипт пакета говорит
`pack.translate("po/ru.po")` до `add_dialog` (документ — `pack.language =
"ru"` до `add_sprites`), и строки пакета становятся русскими. Вместе со
строками меняются буквы, которые пакет печатает, а за ними — шрифты: обе
сборки делают их заново сами.

```
$ cat po/ru.po
msgctxt "hello"
msgid "Hello! I am going for a walk."
msgstr "Привет! Я иду гулять."
...
$ MYGAME_LANG=ru make
the pack: python3 tools/make_pack.py <движок>
mygame.pack: 1192 bytes
...
text: DejaVu Sans Book 20 px (10 pt x 2), 19 glyphs
$ cat main/assets/mygame.letters
 !.ГПЯвгдеийлортуья
```

**Шрифты.** Игра заказывает шрифт размером в точках кадра
(`add_font("text", points=10)`); при масштабе 2 это 20 пикселей дисплея.
Начертание DGX выбирает из шрифтов компьютера по файлу правил игры:

```
source system
allow "DejaVu Sans" 8..
```

Правила, их язык и инструменты `pick` и `list` описаны в начале
`font2c/dgx_fonts.py` DGX. Каждая сборка пишет `fonts-used.txt`: какие
шрифты взяты и что их файлы говорят о лицензии. Можно ли их брать, решает
автор игры.

## 8. Если что-то пошло не так

**Пакет**

| Что видно | Почему | Что делать |
| --- | --- | --- |
| `ModuleNotFoundError: No module named 'dgx_ge_pack'` | скрипт пакета не нашёл инструменты движка | проверить путь к движку, который сборка передаёт скрипту (`ENGINE=`, `GAME_ENGINE`) |
| `No module named 'PIL'` | `python3` без Pillow — часто это Python, который активировал ESP-IDF | `python3-pil` или `pip install Pillow` для системного `python3`; в CMake искать системный (раздел 4) |
| `AnimError`, `DialogError`, `ValueError` с файлом и строкой | ошибка в данных | раздел 2.5; обычно опечатка или ссылка на то, чего нет (или ещё нет: порядок вызовов, раздел 2.1) |
| `the pack was not made: <команда> failed (it says why above)` | так `host.mk` передаёт отказ упаковщика (`GAME_PACK_MAKE`) | читать сообщение выше |
| `the pack could not be made (... tools/make_pack.py: see above)` | так `main/CMakeLists.txt` из раздела 4 передаёт отказ упаковщика | читать сообщение выше |
| `warning: ... is not in set ..., taken as ...` | цвет картинки не из её набора | поправить палитру картинки или набор |

**Настройка и компиляция**

| Что видно | Почему | Что делать |
| --- | --- | --- |
| `#error "the actors of this pack keep N params, the build fewer (DGX_GE_ANIM_PARAMS): dgx_ge_params_of_pack(<this header>) in the game's CMakeLists, GAME_PACK_H in its Makefile of the computer"` | сборка хранит меньше параметров, чем нужно пакету | `dgx_ge_params_of_pack` после `idf_component_register`, `GAME_PACK_H` в `Makefile`; пакет — до чтения заголовка (раздел 4: `execute_process`, `GAME_PACK_MAKE`), а не обычным правилом |
| `<header>: no _ANIM_PARAMS (made by dgx_ge_pack.py with animations?)` | `dgx_ge_params_of_pack` на пакете без анимаций | игре без анимаций этот вызов не нужен |
| `'MYGAME_..._...' undeclared` | имя поменялось в данных | поправить код: заголовок следует за данными |
| `__component_get_property Function invoked with incorrect arguments` из `main/CMakeLists.txt` | в первом чтении компонентов ESP-IDF их свойства недоступны (например, `idf_component_get_property` ради каталога движка) | передавать каталог движка свойством сборки, как в разделе 6 |
| `dgx_ge_fonts(...): DGX 0.7.2 or later is needed` | DGX старее нужного | обновить DGX |
| предупреждение ESP-IDF 6 `Source file .../boards/<плата>/<плата>.c belongs to component dgx-ge but is being built by component main` | игра собирается внутри каталога самого движка (пример в клоне репозитория движка): каталог сборки `main` лежит в каталоге движка | ничего: сборка верна; у игры с движком из реестра или из другого каталога его нет |

**Связывание**

| Что видно | Почему | Что делать |
| --- | --- | --- |
| ``undefined reference to `_binary_mygame_pack_start'`` | имя символа не то: оно делается из имени встроенного файла (`mygame.pack` → `_binary_mygame_pack_start`, `_end`), или файл не встроен | сверить `asm("...")` в коде с `EMBED_FILES` / `GAME_EMBED` |
| нет функций DGX (`dgx_vscreen...`, драйвер дисплея) | DGX собирает только то, что включено в `sdkconfig` | настройки платы из `boards/<плата>/sdkconfig.defaults` в `SDKCONFIG_DEFAULTS` (раздел 6.1); после смены — удалить `sdkconfig` сборки |

**На плате или в окне**

| Что видно | Почему | Что делать |
| --- | --- | --- |
| `dgx_ge_kind` возвращает `NULL` | блоб — не анимация; или анимации нужно больше параметров, чем хранит сборка; или кончились места видов (в лог: `kinds: no place for the kind of blob N, the game said M kinds`) | номер `<P>_BLOB_<ВИД>`; параметры (выше); ёмкость `dgx_ge_kinds_init` — из констант игры |
| `dgx_ge_layout_actor_start` возвращает `false` | нет такого персонажа или вид не открывается | номер `<P>_SCENE_<СЦЕНА>_ACTOR_*`; как выше |
| актёр не виден | цвета его набора не отданы кадру; не тот слой или закрыт более поздним; элемент не добавлен между `dgx_ge_scene_actors_begin` и `dgx_ge_scene_render`; кадр `none` | обзор, часть 3, [«Если что-то пошло не так»](overview-ru.md#если-что-то-пошло-не-так) |
| актёр стоит | событие, на которое у его текущего скрипта нет правила `on`, он не замечает; `dgx_ge_actor_advance` не зовётся; время не в миллисекундах | `tools/dgx_ge_explain.py` покажет, на что скрипт отвечает и на что нет |
| движение зависит от платы | решение принимается «в момент вызова», а не по событию | обзор, [«Время настоящее»](overview-ru.md#время-настоящее) |
| реплика обрезана `...` | текст не влез в место стиля | больше `place` у стиля, или `important` (длинное прокручивается) |
| памяти не хватает | кадр — ширина × высота байт и около 1,5 КБ сверх; декорация — 32 байта; актёр — около двухсот байт; запись мира — 28 байт | меньше кадр или ёмкости сцены (`dgx_ge_scene_create`), записи мира вместо актёров для того, чего не видно; на P4 — PSRAM |
| прошивка падает при старте, на компьютере всё хорошо | стек задачи `app_main` на плате мал, на компьютере — мегабайты | большие массивы — `static` или из кучи ([host-ru.md, раздел 10](host-ru.md#10-чего-компьютер-не-покажет)) |

## 9. Как проверен этот текст

По этому тексту с нуля, в пустом каталоге, собраны две маленькие игры на
движке из репозитория, DGX 0.8.0 и DGX-DI 0.1.2.

- Первая — по входу 2.1: скрипт пакета из раздела 2.1 (три картинки,
  анимация с параметром, диалог со стилем и запрошенным шрифтом, звук,
  число игры), `main.c` из раздела 5, `Makefile` и CMake из разделов 4 и 6.
  Она собрана для компьютера и прогнана без окна (вывод — в разделе 7), с
  переводом на русский и без него, и собрана ESP-IDF 6.0.1 для CYD
  (раздел 6.1). Рост числа параметров с двух до трёх в обеих сборках
  проходит за один запуск: и `idf.py build`, и `make` с `GAME_PACK_MAKE`
  сами собирают пакет заново и всё с новым числом. `make clean` пакета не
  делает; упаковщик, который не нашёл движок, останавливает `make` с
  сообщением `the pack was not made`. Сборка для CYD — без предупреждений
  CMake.
- Вторая — по входам 2.2 и 2.3: документ из раздела 2.2 и скрипт, который
  даёт отсчёты звуку, названному документом, и добавляет цвет неба; сцена разложена через
  `dgx_ge_layout_*`, герой начинает там, где его поставил документ, и
  доходит до зоны выхода. Собрана для компьютера и прогнана без окна, и
  собрана для CYD.
- Отказы раздела 2.5 и сообщения раздела 8 о пути к движку, о `#error`
  параметров, о `__component_get_property` и о символе пакета получены на
  этих же играх; остальные строки раздела 8 взяты из кода движка и DGX.

На самой плате эти две игры не запускались: время кадра и свободная RAM
на CYD для них не измерены.
