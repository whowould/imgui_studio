# ImGui Studio

A visual editor for Dear ImGui interfaces. You build on a canvas that renders with the real widget code, then export clean C++ that draws exactly what you saw. A running program can also follow the project file live.

## Download

Get the zip from [Releases](https://github.com/whowould/imgui_studio/releases), unzip it, and run `bin\imgui_studio.exe`. Keep the `bin\kit` folder next to the exe: exports copy the kit from there. Every push is also built by GitHub Actions.

## Two kinds of project

**New** offers four templates:

| Template | What you get |
| --- | --- |
| **Example menu** | A tabbed menu: categories, a feature list, and settings rows laid out in columns |
| **Loader** | A 400 × 500 app with three screens: sign in, products, and loading |
| **Blank menu** | An empty menu |
| **Blank app** | One empty screen, at any size |

`examples/showcase.imstudio` is an app that uses most features: components, tabs, an image, icons, code-controlled text, a resizable window and the example plugin's widgets.

- **Menus** are a tree: categories (tabs), then groups, features, sections and rows (toggle, slider, dropdown, segmented, chips, colour, keybind, button, value, note).
- **Apps** are a window of any size with **screens** of free-placed **elements**:
  - text, button, input, checkbox, slider, dropdown, progress, list, tabs, panel, divider, spinner, image and icon;
  - **component instances**;
  - **custom** widgets drawn by your own code.
- A button can open another screen, close the window, show a notification, and call your code.

## Designing

Tab switches between **Design** and **Interact**.

In **Interact**, everything works as it will in your program, buttons change screens, and the values you leave set become the exported defaults.

In **Design**:

- **Click** an element to select it; **Shift+click** adds to the selection.
- **Drag** on an empty spot to select a group with a box.
- **Move and resize:**
  - Drag to move, or use the eight handles to resize (Shift keeps the shape).
  - Elements snap to the window's edges, centre and 32 px margins, and to each other, with guide lines. Alt turns snapping off.
  - Arrow keys nudge by 1 px, or 8 px with Shift.
- **Several selected:** align (left, centre, right, top, middle, bottom), even the gaps between them, or match their sizes.
- **The window itself:** drag its right or bottom edge to resize it. Elements follow their **pins** (left/right/stretch/centre across, top/bottom/stretch/middle down). The same pins apply at runtime if the window is set to **Resizable**.
- **Zoom:** Ctrl+scroll, Ctrl +/−, Ctrl 0 to fit, Ctrl 1 for 100%. Middle-drag or Space+drag pans.
- **Copy, cut, paste and duplicate:** Ctrl C, X, V and D work on elements and menu rows, including between projects.
- **Components:** select some elements and choose **Make component** to turn them into a reusable component. Edit it once, under Components, and every instance follows. **Detach** turns an instance back into plain elements.

## Styles

Project settings controls the look of both menus and apps:

- **Palette:** Graphite, Midnight, Light, OLED, or **Custom**.
  - Custom takes a window colour, a text colour and a muted text colour, with an option for a light window.
  - **Save** stores your custom palette for every project; **Load** brings it into the current one.
- **Accent colour and window opacity.**
- **Corners:** separate radii for the window and the controls.
- **Widget styles:** toggles as switches or checkboxes, sliders as a bar or a track, buttons as soft, solid or outline, and fields as boxes or underlines.
- **Screen changes:** none, fade or slide.

Elements can also override their own colour.

## Your code in control

Set these per element:

- **Changeable text:** a text element becomes a `std::string` in `state`, so your code can show live status.
- **Can hide / Can disable:** adds a `bool` to `state` (`…_visible`, `…_enabled`).
- **Show on:** puts the element on one tab of a Tabs element.

## Images and icons

**Import image…** (PNG, JPEG, BMP or GIF) stores the image in the project. The export embeds it as a byte array.

The kit decodes images with Windows Imaging Component and uploads them through ImGui 1.92's texture system, so any renderer backend that supports `ImGuiBackendFlags_RendererHasTextures` works.

There are 39 built-in line icons, available on buttons and as Icon elements.

## Custom widgets and plugins

A **Custom** element calls a function you write: `void name( const ImRect& r )`.

To see it on the canvas too, build it into a widget plugin. See `examples/widget_plugin/widgets.cpp`: the main build produces `bin\plugins\widgets.dll` with two sample widgets.

1. Load the DLL under Project settings > Custom widgets.
2. Pick a function name. The editor reloads the DLL whenever you rebuild it.
3. Compile the same source file into your app; the export calls the function by name.

## Using an export

**A menu:**

```cpp
#include "menu.h"

menu::setup( ImGui::GetIO( ) ); // once, before the font atlas is built
menu::draw( );                  // every frame, between NewFrame and Render

if ( menu::settings.aimbot && menu::settings.aimbot_smoothness > 2.f ) { /* ... */ }
```

**An app:**

```cpp
#include "loader.h"

loader::setup( ImGui::GetIO( ) );
loader::events.sign_in = [] { check_key( loader::state.sign_in_username, loader::state.sign_in_password ); };
loader::events.launch = [] { start_injection( loader::state.products_products ); };

// every frame
loader::state.loading_status = "Injecting\xe2\x80\xa6";
loader::draw( ); // nothing is drawn once a "close" button clears loader::state.open
```

The app export also provides:

- `screen_t`, with every screen;
- `state_t`, with every value, flag and the current screen;
- `events_t`, with one `std::function` per button.

Setting `state.screen` yourself switches screens.

To build it, add the generated `.cpp` and `kit.cpp` to your project, and put the export folder on your include path. You need ImGui 1.92 or newer and C++20. `kit.h` defines `IMGUI_DEFINE_MATH_OPERATORS`, so include it (or the generated header) before any other ImGui header in the same file.

## Live link

A running program can follow the project file instead of its compiled layout:

1. Define `KIT_LIVE` and add `kit_live/*.cpp` from the export.
2. Call `<namespace>::live( "path/to/project.imstudio" )` once after `setup( )`.

Each time you save in ImGui Studio, the running program reloads the layout, look and text. Your values, events and custom widgets stay connected by name. Leave `KIT_LIVE` out of release builds.

## Building

You need Visual Studio 2022 with the C++ and CMake components.

```bat
build.bat
```

This produces:

- `bin\imgui_studio.exe`;
- the kit and live-link sources in `bin\kit`, which exports copy from;
- the example plugin, `bin\plugins\widgets.dll`.

## Command line

```bat
imgui_studio.exe project.imstudio
imgui_studio.exe --template loader
imgui_studio.exe project.imstudio --export out\menu
imgui_studio.exe --template loader --save loader.imstudio
```

- `--template` starts from `example`, `loader`, `blank-menu` or `blank-app`.
- `--export` and `--save` run without a window.

## Saving and recovery

- **Open** lists recent projects.
- Unsaved work is copied to `%APPDATA%\ImGui Studio` every few seconds. If the editor closes unexpectedly, the next start offers to restore it.

## Shortcuts

| Keys | Action |
| --- | --- |
| Tab | Design / Interact |
| Ctrl Z / Ctrl Y | Undo / redo |
| Ctrl S, Ctrl Shift S | Save, save as |
| Ctrl O | Open |
| Ctrl E | Export |
| Ctrl C / X / V / D | Copy, cut, paste, duplicate |
| Ctrl A | Select every element on the screen |
| Delete | Delete the selection |
| Escape | Select the screen |
| Arrows, Shift + arrows | Nudge 1 px / 8 px |
| Ctrl + scroll, Ctrl + / −, Ctrl 0, Ctrl 1 | Zoom, fit, 100% |
| Middle-drag, Space + drag | Pan |
| Shift while resizing a corner | Keep the shape |
| Alt while dragging | Turn snapping off |

## Layout

| Path | Contents |
| --- | --- |
| `kit/` | The widget library shipped with every export, plus `studio_plugin.h` for widget plugins |
| `src/document.*` | Project model, templates, clipboard and the `.imstudio` JSON format |
| `src/names.*` | The C++ names of a project's values, shared by the export and the live link |
| `src/preview.*` | Renders a project through kit (the canvas and the live link) |
| `src/codegen.*` | Turns a project into C++ |
| `src/live.*` | The live-link runtime shipped with exports |
| `src/plugins.*` | Loads widget plugins |
| `src/editor.*` | The editor UI |
| `src/platform.*` | Borderless Win32 window, DX11, file dialogs |
| `examples/` | The showcase project and the widget plugin example |
