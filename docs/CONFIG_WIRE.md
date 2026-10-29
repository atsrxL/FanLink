# FanLink persistent settings wire contract — 0.2.0 candidate

All values little-endian. Settings/RF payload bytes remain unchanged by ADR-025,
but every outer Host envelope now uses version2. The table below describes the
inner RF/settings version1 payload, not the Host frame version. Keep Host header8,
CRC2 and MAX_PAYLOAD128; do not accept v1 outer frames or add a compatibility
adapter. Network/session detect mistakes, not authentication.

Host sends type0x15 with30-byte request; TX replies type0x93 with30-byte response
and the same Host sequence. For node0 handle locally on TX; nodes1..8 are routed
to RF. RF request/reply types0x40/0x41 use the same30-byte payload. Only a matched
node replies; other nodes must not respond. Session is a nonzero32-bit request
transaction ID. Replies must match network,session,node,operation and value.

| Offset | Size | Request | Reply |
|---|---:|---|---|
|0|2|F1 A5|F1 A5|
|2|1|version1|version1|
|3|1|0x40|0x41|
|4|4|network ID|echo|
|8|4|nonzero session|echo|
|12|1|target0 TX /1..8 RX|echo|
|13|1|op1 Query /2 SetPinMode /3 SetPower|echo|
|14|1|query0; mode0 UART/1 PWM/2 PWMSWAP; signed chip dBm power|echo|
|15|1|0|status0 Ok/1 BadRequest/2 Unsupported/3 Busy/4 StorageError/5 RadioError|
|16|1|0|profile1 Lite /2 Nano|
|17|1|0|selected mode0/1/2; legacy safe=254 accepted for explicit repair; TX=255|
|18|1|0|selected signed chip dBm power|
|19|1|0|flags bit0 pending restart/apply; bit1 invalid persistent record (validated compiled defaults under ADR-026)|
|20|4|0|capability bitmask|
|24|2|0|OTA image-byte cap5120|
|26|1|0|max receivers8|
|27|1|0|normal poll period125ms|
|28|2|CRC16/CCITT-FALSE over0..27|same|

Capabilities bits:0 softwareBOOT,1 localUART OTA,2 TX-to-RX RF OTA,
3 RX RF OTA,4 RX pin modes,5 power setting,6 persistent settings.
TX advertises0/1/2/5/6; normal/unified RX advertises3/4/5/6.
UART-mode RX retains RF OTA and settings; this is not a transparent CRSF bridge.

Power uses chip dBm to avoid confusing external PA input with radiated power:
Lite accepts -18..13dBm; Nano accepts the official table -10/-6/-3/1dBm
(nominal10/25/50/100mW board levels, not measured). Current TX and all RX builds
are BETAFPV Lite, default13; Nano bounds remain protocol support only. Existing
Nano records are rejected by Lite firmware: use validated compiled defaults
and report flags bit1 until an explicit setting save. Valid same-profile,
same-node/network saved values override defaults.
CLI must show both profile and allowed values and reject unsupported
requests. Pin modes do not change each board's established PWM gate polarity.

TX suppresses ordinary duty updates during config maintenance and broadcasts
full speed for at least600ms before mutating requests. Query requires no flash.
Setting acknowledgements confirm validated storage/scheduling, not physical
pin behavior, actual RF output, reboot or peer reception. Use a2.5s grace for
retry before reboot/apply; duplicate identical requests do not rewrite Flash or
indefinitely postpone the action. RX replies Busy to conflicting settings while
pending; TX answers local requests Busy when another maintenance owner is active.
TX drops conflicting requests while its config transaction owns the bridge, and
drops remote config during other maintenance rather than inventing an RX profile;
the Host sees a timeout and must retry after that operation finishes.
A query after reboot verifies selected settings. Power changes also
may reboot for atomic application; callers must tolerate loss of the old link.

Settings storage must validate magic/schema/profile/network/node/CRC and bounded
values; erased storage uses compile-time defaults. Under ADR-026 corrupt or
unreadable storage also uses validated compiled defaults, with flags bit1 set.
No automatic persistence occurs; explicit mode or power save repairs the record.
Legacy mode254 plus bit1 remains accepted for explicit old-image repair.
Invalid compiled defaults are not operational fallback candidates.
Write failures must not silently apply/reboot a new selection.
No ACK to a wrong network/node, bad CRC or malformed envelope. No implicit
Flash writes on boot/query/normal poll. Firmware OTA keeps configuration storage.
Storage uses one reserved EEPROM sector with checked erase/write/readback, not a
power-fail-atomic journal. Power loss precisely after erase can leave all-FF and
therefore restore compile-time defaults on next boot; ordinary successful saves
survive reboot/OTA. Do not power off during a setting write. TX local settings
will not auto-reboot until its ACK was accepted into the UART FIFO; if FIFO stays
blocked it remains full-speed/pending until the ACK can be written.

Production gateway migration: TX/C6/PC use native eight-slot Host v2, with
SET_ALL8-byte, STATUS54-byte and METRICS16-byte payloads; no nodes9..16, padding,
truncation or dual stack. Preserve2500ms telemetry freshness,5miss/6s presence,
fresh-reply recovery and the existing Host transport trust boundary. Old v1 TX
requires archived v1 tools or separately authorized wired ROM migration; new
v2 tools cannot trigger its BOOT/OTA. RF settings/OTA remain compatible, so RX
needs no immediate reflash for this Host change. No device flashing or C6
deployment is part of this migration; old feature evidence is not v2 acceptance.
