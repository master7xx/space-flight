# Space Flight

[Русский](README.ru.md) · **English** · [Español](README.es.md)

A small, peaceful 2D game about flying among stars and asteroids. Lasers mine ore, a branching upgrade tree improves your ship, and exploration unlocks sectors, expeditions, and rare encounters. Fly manually or let the autopilot take over. Made **just for fun**.

The game supports Russian, English, and Spanish. Press **L** to switch languages; an on-screen hint shows the key. The choice is saved separately from game progress.

**Play on Windows x64:** read the [end-user terms](EULA.md), then download `SpaceFlight-Win64.exe` from the [latest release](https://github.com/master7xx/space-flight/releases/latest). The executable is unsigned, so Windows may display a security warning.

## Controls

| Keys | Action |
|---|---|
| A / D or ← / → | Manual flight |
| F | Autopilot |
| Tab | Upgrade tree |
| Mouse | Buy an upgrade; drag empty tree space or use the middle / right button; wheel to zoom |
| G / T / B / R / J | Next upgrade / track / auto-buy / center tree / journal |
| X | Laser target priority |
| C | Change unlocked sector |
| E | Expeditions and rewards |
| I / M / K | Refining / fabricator / specialization; Enter to confirm, Esc to cancel |
| H | Detailed HUD and help |
| L | Русский / English / Español |
| − / + | Star brightness |
| Esc | Pause or close a panel |
| N, then N while paused | Start a new flight and reset progress |
| P | Stress mode without saving progress |
| Q | Quit |

## Running and building

Install Unreal Engine 5.8 and a compatible Windows C++ toolchain. Open `SpaceFlight.uproject` in Unreal Editor and start the game, or set `UE_EDITOR` to the full path of `UnrealEditor.exe` and run `Play.bat`. Unreal assets are in `Content`; source art and audio are in `Art`.

Saves and local settings stay on your computer and are excluded from the repository. C++ tests are in `Source/SpaceFlight`; audio-file tests require Python and NumPy.

Project license: [MIT](LICENSE). Unreal Engine is subject to Epic Games' separate terms.
