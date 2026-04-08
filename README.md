# SIMCom A7682E CLI

Small C++ command-line tool for talking to an A7682E modem over its AT command control port.

The current goal is manual configuration and monitoring from a terminal. The code is split into a reusable serial/AT layer so it can later grow into a DBus-backed modem manager.

## Modem Port Layout

From `doc/A76XX Series_Linux_USB_Application Note_V1.00.pdf`:

- `/dev/ttyUSB0`: diagnostic output
- `/dev/ttyUSB1`: AT command control port
- `/dev/ttyUSB2`: PPP modem/data port
- `/dev/ttyUSB3`: NMEA port when GNSS is enabled

This tool defaults to `/dev/ttyUSB1`.

## Build

```sh
make
```

The binary is written to `bin/simcom_cli`.

## Usage

High-level commands:

```sh
./bin/simcom_cli info
./bin/simcom_cli get signal
./bin/simcom_cli get operator
./bin/simcom_cli get network-mode
./bin/simcom_cli set cmee 2
./bin/simcom_cli set apn 1 internet
./bin/simcom_cli set operator auto
./bin/simcom_cli set network-mode lte
./bin/simcom_cli set reset
./bin/simcom_cli set usbnetmode 1
```

Raw AT mode still works:

```sh
./bin/simcom_cli AT+CSQ
./bin/simcom_cli +CPIN?
./bin/simcom_cli raw AT+COPS?
```

Interactive shell:

```sh
./bin/simcom_cli --interactive
```

Interactive shell notes:

- high-level commands such as `info`, `get signal`, and `set cmee 2` work in the shell
- raw AT commands such as `AT`, `ATI`, `AT+CSQ`, and `AT+CEREG?` also work
- commands starting with `+`, `$`, `&`, or `*` are automatically prefixed with `AT`
- local shell commands use a leading `:`
- `:help` shows interactive help
- `:raw on` or `:raw off` toggles raw line printing
- `:timeout 5000` changes the per-command timeout
- `:quit` exits

Monitor unsolicited modem output:

```sh
./bin/simcom_cli --monitor
```

This mode just prints incoming modem lines with timestamps until `Ctrl-C`.

## Named Commands

`info` runs a small status bundle:

- manufacturer
- model
- firmware
- IMEI
- SIM state
- signal quality
- operator
- EPS registration
- packet attach state
- PDP contexts
- modem functionality state

`get <name>` currently supports these aliases:

- `id`, `identity`
- `manufacturer`, `model`, `firmware`
- `imei`, `imsi`
- `sim`, `pin`
- `signal`, `csq`
- `operator`, `cops`
- `reg`, `network`, `eps-reg`, `cereg`
- `ps-reg`, `cgreg`
- `cs-reg`, `creg`
- `attach`, `cgatt`
- `pdp`, `contexts`, `apn`, `cgdcont`
- `active-contexts`, `cgact`
- `clock`, `cclk`
- `errors`, `cmee`
- `functionality`, `fun`, `cfun`
- `activity`, `cpas`
- `network-mode`, `netmode`, `cnmp`

`set <name> <value...>` supports:

- generic AT setters such as `set cmee 2` -> `AT+CMEE=2`
- `set apn <cid> <apn> [pdp_type]`
- `set operator auto` -> `AT+COPS=0`
- `set network-mode <auto|gsm|lte>` -> `AT+CNMP=<...>`
- `set reset` -> `AT+CFUN=1,1`
- `set usbnetmode <0|1>` -> `AT$MYCONFIG="usbnetmode",<value>`
- `set dialmode <0|1>` -> `AT+DIALMODE=<value>`

For unusual commands or exact quoting requirements, use raw AT mode directly.

## Supported Human-Readable Parsers

The CLI currently decodes these common status/configuration commands:

- `ATI`
- `AT+CGMI`
- `AT+CGMM`
- `AT+CGMR`
- `AT+CGSN`
- `AT+CIMI`
- `AT+CPIN?`
- `AT+CSQ`
- `AT+CREG?`
- `AT+CGREG?`
- `AT+CEREG?`
- `AT+COPS?`
- `AT+CGATT?`
- `AT+CGACT?`
- `AT+CGDCONT?`
- `AT+CCLK?`
- `AT+CMEE?`
- `AT+CFUN?`
- `AT+CPAS`
- `AT+CNMP?`

Unknown commands still work; the CLI falls back to printing the modem's raw response lines.

## Notes

- The tool currently handles classic line-oriented AT responses ending in `OK`, `ERROR`, `+CME ERROR`, `+CMS ERROR`, `CONNECT`, `NO CARRIER`, `NO ANSWER`, or `BUSY`.
- It is not yet a full URC-aware modem manager. That is the next logical step once the manual CLI workflow is stable.
- For clearer errors, `AT+CMEE=2` is useful during manual sessions.
