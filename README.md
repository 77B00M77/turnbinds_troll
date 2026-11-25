<h1 align="center"><img src="https://user-images.githubusercontent.com/16616463/182579363-4bce6231-b03d-40fb-859e-2b49b4929c15.png" width="24" height="23"> turnbinds</h1>

<p align="center">
<img src="https://github.com/user-attachments/assets/fb9c403f-cd3b-412d-911d-9a3a7ade4b8a">
</p>

Provides external turnbinds (`+left/+right`) with customizable yawspeed for movement game modes in Source games.

This program aims to fill the gap created by Valve disabling access to the `cl_yawspeed`, `cl_anglespeedkey` convars in the most recent versions of Counter-Strike (CS:GO, CS2), forcing useless unchangable defaults.

These convars, which control the turning speed for the `+left/+right` commands, are essential for movement game modes like surf.

- [Usage](#usage)
- [Fork additions](#fork-additions)
- [Anti-cheat software](#anti-cheat-software)
- [Development](#development)

> This is a fork of [t5mat/turnbinds](https://github.com/t5mat/turnbinds) (MIT, see [LICENSE.md](LICENSE.md)). All credit for the original program goes to its author. The fork only adds the spin binds described under [Fork additions](#fork-additions); the original turnbinds behave exactly as before.

## Usage

[Download](https://github.com/t5mat/turnbinds/releases/latest/download/turnbinds.exe) and run `turnbinds.exe`.

Settings are stored in `<exe-name>.ini`.

### Basic controls

This fork replaces the original console screen with a settings window:

- **Key buttons** - click, then press the key (or mouse button) to bind. **Esc** cancels
- **Text boxes** - type values; changes apply instantly. Invalid values turn red and the program pauses until they're fixed
- **◀ / ▶** - step through the active value set (same as pressing the *cycle* key)
- **Hover** over any setting for a short explanation
- Close the window to quit; settings are saved to the `.ini` on close

### Variables

- *cl_yawspeed* - The desired turning speed
- *sensitivity* - Should match your in-game `sensitivity` value
- *cl_anglespeedkey* - The factor by which *cl_yawspeed* is scaled while holding down the *+speed* key
- *m_yaw* - Should match your in-game `m_yaw` value

Each of these can have multiple values (space-delimited), which are cycleable while in-game by pressing the *cycle* key (less useful for *m_yaw*).

You can use this program in a number of different ways,
- cycle between multiple *cl_yawspeed* values while using *cl_anglespeedkey* as a constant "slowdown" factor (the more common approach)
- have a single sensible *cl_yawspeed* and cycle between multiple *cl_anglespeedkey* values
- cycle both *cl_yawspeed* and *cl_anglespeedkey*

<blockquote>

<p>Optionally, you'd might want to cycle between <i>sensitivity</i> values (increasing both <i>cl_yawspeed</i> and <i>sensitivity</i> for sharper turns).</p>
<p>In this case, the program's <i>sensitivity</i> and the in-game <code>sensitivity</code> need to be kept in sync. This is because the program is external to the game and cannot control your in-game <code>sensitivity</code> - <i>sensitivity</i> values in the program are only used to calculate the correct turning speed.</p>
<p>You'd want to bind your <i>cycle</i> key in-game to cycle between your <i>sensitivity</i> values, and make sure the in-game <code>sensitivity</code> and the program's current <i>sensitivity</i> value are the same.</p>

<p>
So, for this config:
<pre>
cycle             : X1 Mouse Button
cl_yawspeed       : 120 <i>[210]</i> 300
sensitivity       : 1.0 <i>[2.0]</i> 3.0
</pre>
</p>

<p>
Run the following in-game commands:
<pre>
bind MOUSE4 "toggle sensitivity 1.0 2.0 3.0"
sensitivity 2.0
</pre>
</p>

</blockquote>

### Developer settings

Shown in the *General* section of the window.

- *rate* (default 1000) - maximum number of simulated mouse inputs per second, lower values decrease CPU usage in favor of turn smoothness
- *sleep* (default 3500) - main loop sleep duration (measured in 100ns units, 3500 = 0.35ms), higher values decrease input polling rate and overall CPU usage

## Fork additions

- A settings window instead of the console screen (see [Basic controls](#basic-controls)). The turning behaviour is unchanged.
- Spin binds (below).

Note: the *Download* link above is the original program, without these additions. Build this fork yourself (see [Development](#development)).

### Spin binds

Two extra binds that turn your view by an exact number of full rotations (a "perfect 360"), mostly for trolling spectators.

- *spin* - a fast spin
- *slow spin* - a slow-motion spin, for maximum disrespect

Both are unbound by default. Click its button and press a key to bind it, or click **clear** to unbind it.

### Spin variables

- *spin_duration* (default 0.4) - how long a *spin* takes, in seconds
- *spin_count* (default `1 2`) - rotations per *spin*. Can have multiple values (space-delimited), cycled by the *cycle* key (or the ◀ / ▶ buttons next to *active spin_count*), so by default *cycle* switches between a 360 and a 720
- *slowspin_duration* (default 2.5) - how long a *slow spin* takes, in seconds
- *slowspin_count* (default 1) - rotations per *slow spin*
- *spin_direction* (default right) - left or right, shared by both spins

These are saved to the `.ini` like everything else. An invalid spin value only disables that spin; it never puts the program into the error state.

### How it works

- The number of mouse counts is `counts = (360 * rotations) / (sensitivity * m_yaw)`, using the currently active (cycled) *sensitivity* and *m_yaw* values.
- The movement is sent smoothly over the duration through the same simulated mouse input as the turnbinds, limited by the *rate* setting (it isn't one big jump).
- Mouse counts are whole numbers, so each spin sends a whole number of counts and carries the leftover fraction over to the next spin. Repeated spins never drift: you always end up within half a mouse count of the starting angle.
- Pressing a spin key while a spin is in progress is ignored (spins don't stack).
- Spins work alongside the turnbinds: holding *+left/+right* during a spin simply adds to it.
- If the program becomes inactive mid-spin (e.g. a menu opens and the cursor appears), the spin is cancelled.
- The settings window also shows how many mouse counts a 360 is at your current sensitivity and m_yaw.

**For the spin to land exactly on 360, the program's *sensitivity* and *m_yaw* must match your in-game values** (and with cycled sensitivities, the currently highlighted one must match what's active in-game).

## Anti-cheat software

The program does not patch or inject anything into the game.

Apart from simulating mouse input, it doesn't really do anything suspicious.

It would be fair to say it's as VAC bannable as an AutoHotkey script.

Standalone anti-cheat software (FACEIT AC, ...) can easily detect mouse movement simulation, and either prevent it or completely prevent the program from running.

## Development

To build into `./build`:

```docker build . -o ./build --progress=plain```

Native Windows build without Docker (fork addition): with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (the same toolchain the Dockerfile uses), CMake 3.28+ and Ninja on `PATH`:

```
cmake -S . -B build -G "Ninja Multi-Config" -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
cmake --build build --config Release
```

The executable ends up in `build/Release/turnbinds.exe`.
