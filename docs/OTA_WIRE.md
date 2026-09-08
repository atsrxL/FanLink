# RF OTA v1 implementation contract

## 0.1.9-dev optional block extension

Existing six operations and packet layout remain byte compatible. Op7 Fragment
uses the existing ten data bytes and absolute image offset, aligned to ten bytes
within a 100-byte block; Op8 BlockFinish uses offset=block end and zero data.
Block size is min(100, image_size - block_start); block_start is a multiple of100.
Fragments receive no ACK. The addressed RX buffers a bitmap and at most100 bytes,
accepts identical duplicates/out-of-order fragments within the current block,
and writes only after a matching BlockFinish with every fragment present. Missing
fragments return BadOffset; host retries the complete identical batch. Altered
duplicates/padding return BadImage on BlockFinish. Completed last-block bytes are
retained to validate retransmissions; duplicate BlockFinish returns its cached ACK
without another write. Fragment traffic alone never refreshes progress timeout.
Legacy Data and Fragment modes cannot mix in one session. Finish/Commit remain
the same operations with complete image verification and explicit boot commit.

New Host request0x11 payload is a complete CRC-valid30-byte Op7 request holding
the first min(10,count) bytes, followed by the remaining count-first bytes.
Thus payload is30..120 bytes and still fits the existing128-byte Host limit.
TX requires an active matching Begin session and exact inferred payload length.
It transmits fragments with1000us post-transmit guard, then Op8 BlockFinish and
waits for one ordinary Host0x90-wrapped OTA ACK with operation8, exact end offset,
Receiving state and expected compatibility. No fake success ACK is synthesized.
Normal Host0x10 cannot inject fragment/block-end operations outside this relay.
RF modulation, fixed30-byte receive length and normal Control/RPM are unchanged.

Use uploader --mode fast only once both running devices support this extension.
Default --mode legacy remains compatible with old0.1.8 RX and new TX, including
uploading a0.1.9 image for migration. No automatic fallback mixes modes. Fast
transmissions including retries are paced with a maximum3072 image bytes/s and
no accumulated credit. This cap/target is not a measured throughput guarantee.

## Legacy operations (retained)

Normal 30-byte Control/RPM packets and Host messages retain existing semantics.
OTA request type 0x30, reply 0x31: bytes 0..1 F1 A5, 2 version1, 3 type,
4..7 network uint32 LE, 8..11 nonzero session uint32 LE, 12 node1..16,
13 operation, 14..17 offset uint32 LE, 18..27 ten data bytes,
28..29 CRC16 CCITT-FALSE over0..27. All unused request bytes must be zero.

Operations: Begin=1 (offset=image size; data[0..3]=compatibility uint32),
Hash=2 (offset0 carries first10 MD5 bytes; offset10 carries last6),
Data=3 (offset=image byte offset, <=10 bytes, final count inferred from size),
Finish=4 (offset=image size), Abort=5 (offset0), Commit=6 (offset=image size).
Replies echo operation, carry next expected image offset (zero before data),
data[0]=status, data[1]=state, data[2..5]=compatibility; remaining bytes zero.
Status: Ok=0, BadState=1, BadTarget=2, BadOffset=3, BadImage=4,
StorageError=5, Busy=6. State: Idle=0, Manifest=1, Receiving=2,
Verified=3, Committed=4, Failed=5. Wrong network/node/session/envelope gets no reply;
a different session Begin while active may get Busy using the request session.
Identical last successful request retries return cached ACK without repeating
flash writes or renewing progress timeout; changed duplicates must be rejected.
Exactly ordered hashes/data only; no gap/overlap, oversize or zero-size images.
Begin activates full speed before storage operations; hash completion begins
staging; Finish checks digest WITHOUT scheduling boot; Commit alone schedules
boot. Commit remains idempotent until reboot, Abort cannot undo committed image.
Timeout10s aborts uncommitted staging; after failure/timeout/Abort a new Begin requires a fresh session; timestamp subtraction is wrap safe.
Committed reboot grace2500ms is measured from first successful Commit only.

Host request type0x10 payload=one complete encoded30-byte OTA request.
Host reply type0x90 payload=encoded30-byte OTA reply, sequence echoes Host request.
TX first accepted Begin locks out duty/heartbeat and drains old control state,
broadcasts all255 for600ms, then exchanges maintenance frames. It waits up to
300ms for each matching RF reply. Host retries same request with 800ms timeout
(first Begin2000ms), maximum12 attempts. After an Abort ACK, TX retains maintenance for retries; idle10s releases TX
maintenance to full-speed controller state requiring fresh Host control. The Host
parser and buffered bytes are cleared at release. A TX
RF timeout emits no fake receiver ACK. Commit does not constitute reboot/health
verification. Host must present this distinction.

RX fingerprint is a stable32-bit FNV-1a over build profile fields (defined by
firmware/include/fanlink/ota_identity.hpp), including node/network/pins/RF config.
Images embed a single16-byte marker FANLINK-OTA-V1!! followed by little-endian
compatibility uint32, network uint32, role byte(1TX/2RX), node byte, format1 byte,
reserved0 byte. Host checks exactly one compatible marker and role/node/network.
RF receiver matches fingerprint before staging. This guards accidental wrong
images; it does not cryptographically bind image content to target metadata.
