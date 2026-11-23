# Configuration

Pip-OS reads `/PIPBOY/PIPOS.CFG` from the SD card at boot. The file is optional. Every key in it is optional too.

## Format

The file is INI text, parsed by RobCo-Core's `ini.h`. The syntax rules are in the [configuration files reference](https://github.com/RobCo-Industries/RobCo-Core/blob/796607fc019706694e226c9b77d05ffa431fec1b/docs/guide/api/buses.md#configuration-files-inih). Section and key names are case-sensitive. The file may hold at most 4096 bytes.

```
[display]
scheme = amber
line_delay_ms = 150

[audio]
volume = 80
beep = yes
```

`boot/PIPOS.CFG` is the sample that `make sdimage` and `make bootdir` put on the card.

## Keys

| Section | Key | Values | Default | Effect |
|---|---|---|---|---|
| `display` | `scheme` | `green`, `amber`, `blue`, `white`, any case | `green` | Colour scheme of the screen console |
| `display` | `line_delay_ms` | 0 to 2000 | `0` | Pause between two boot lines, in milliseconds |
| `audio` | `volume` | 0 to 100 | `100` | Audio volume in percent |
| `audio` | `beep` | `yes`, `no`, `true`, `false`, `on`, `off`, `1`, `0`, any case | `yes` | Beep once at power-on |

Numbers are unsigned decimal. A `+` sign is accepted.

## Errors

No configuration problem stops the boot. The kernel prints one marker per problem, then one summary marker with the settings in use.

| Case | What happens | Marker |
|---|---|---|
| No card | Defaults | `ev=config rc=nocard` |
| No file | Defaults | `ev=config rc=nofile` |
| File larger than 4096 bytes | Defaults | `ev=config rc=toobig` |
| Read error | Defaults | `ev=config rc=io` |
| A line that is not INI | Defaults for every key. `line` names the bad line. | `ev=config rc=syntax line=<n>` |
| Unknown section or key | The key is skipped | `ev=config rc=unknown key=<section>.<key> line=<n>` |
| Bad value | The key keeps its default | `ev=config rc=badvalue key=<section>.<key> line=<n>` |

The summary marker:

```
@RC stage=pipos ev=config rc=ok path=/PIPBOY/PIPOS.CFG line=0 scheme=amber line_delay_ms=150 volume=80 beep=1
```

On a syntax error, keys before the bad line may already have printed `unknown` or `badvalue` markers. None of them applies.
