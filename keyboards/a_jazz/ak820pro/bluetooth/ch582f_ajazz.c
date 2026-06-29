#include "ch582f_ajazz.h"
#include "bluetooth.h"
#include "quantum.h"
#include "hal.h"

#ifndef CH582_SERIAL_DRIVER
#    define CH582_SERIAL_DRIVER SD2
#endif

#define TX_MAX_PAYLOAD 16

/* How often to re-issue the channel-select command while a connection is
 * requested but not yet established. At cold boot the dip-switch callback fires
 * a one-shot 0xA6 microseconds after sdStart(), before the CH582F has finished
 * its own power-up, so that first command is dropped. Retrying until the module
 * answers with a connect event makes boot-in-BT work. */
#ifndef CH582_CONNECT_RETRY_MS
#    define CH582_CONNECT_RETRY_MS 500
#endif

/* How often to poll the module for battery level. The module is silent in steady
 * state; the stock firmware periodically sends `A6 53` and the module answers with
 * a `5C <pct>` frame. (The LCD clock is NOT on this wire: the set-time utility
 * talks to the MCU directly, so time never crosses the CH582 serial link.) */
#ifndef CH582_BATTERY_POLL_MS
#    define CH582_BATTERY_POLL_MS 5000
#endif

static volatile bool    is_module_connected = false;
/* Active BT slot 1-4. Derived from the selected 0xA6 profile, NOT from the 5B
 * stream: a logic-analyzer capture proved the 5B second byte is a handshake-STAGE
 * code (0x23/0x31..0x34 seen regardless of slot), not the slot number. 0 = none. */
static volatile uint8_t connected_slot = 0;

/* True while the module is advertising / waiting to bond (5B 31). */
static volatile bool    is_pairing = false;
/* Battery percentage (0-100) from 5C frames; 0xFF until the module first reports. */
static volatile uint8_t battery_level = 0xFF;
/* Host keyboard LED bitmap from 5A frames (USB LED bits; bit1 = caps lock). */
static volatile uint8_t host_leds = 0;

/* Pending connection request: the profile the user asked for, retried until the
 * module reports connected (or the request is cleared by leaving BT mode). */
static volatile bool    connect_requested = false;
static volatile uint8_t requested_profile = 0;
/* True while the mode slider is in the USB position (wireless link not in use).
 * Set from the dip-switch handler via ch582_cancel_connect / ch582_set_profile. */
static volatile bool    usb_mode = false;
static uint16_t         last_attempt_time = 0;
/* Last time a battery poll (A6 53) was sent. */
static uint16_t         last_battery_poll = 0;

/* Telemetry trackers */
volatile uint32_t debug_tx_packet_count = 0;
volatile uint32_t debug_rx_byte_count   = 0;
volatile uint32_t debug_rx_packet_count = 0;
volatile uint8_t  debug_last_rx_type    = 0;
volatile uint8_t  debug_last_rx_data    = 0;
/* Count of A1 keyboard reports actually forwarded to the module (diagnostic:
 * lets the LCD show whether wireless keystrokes are leaving the SN32 at all). */
volatile uint32_t debug_kbd_tx_count    = 0;

/* Raw RX capture for protocol RE. Since console output is dead on this board, we
 * freeze the first CH582_CAPTURE_N bytes of the live stream into this buffer and
 * render it as paged hex on the LCD. Reset to grab a fresh burst. */
#define CH582_CAPTURE_N 96
volatile uint8_t  ch582_rx_capture[CH582_CAPTURE_N];
volatile uint16_t ch582_rx_capture_len = 0;

void ch582_capture_reset(void) {
    ch582_rx_capture_len = 0;
}

/* Request a battery-level report from the module. Logic-analyzer-decoded from the
 * stock firmware: the MCU sends `A6 53` and the module replies with a `5C <pct>`
 * frame (parsed in ch582_task). The module is otherwise silent, so battery only
 * updates if we poll. */
#define CH582_BATTERY_REQ_PARAM 0x53
void ch582_poll_status(void) {
    uint8_t param = CH582_BATTERY_REQ_PARAM;
    ch582_send_command(0xA6, &param, 1);
}

typedef enum {
    STATE_WAIT_TYPE = 0,
    STATE_WAIT_DATA,
    STATE_WAIT_CHECKSUM
} parse_state_t;

static const SerialConfig serial_cfg = {
    .speed               = 115200,
    .UART_WordLength     = 3,  // 8 data bits
    .UART_StopBits       = 0,  // 1 stop bit
    .UART_Parity         = 0,  // No parity
    .UART_FIFOControl    = 0,
    .UART_AutoBaudControl = 0,
    .UART_Oversampling   = 16,
    .UART_HalfDuplexMode = 0,
};

/* True when key reports should be forwarded to the module over UART: a BT/2.4G
 * profile is selected (dip switch in a wireless position) AND the link is up.
 * In USB mode connect_requested is cleared, so we never double-type. */
static bool ch582_kbd_output_active(void) {
    return connect_requested && is_module_connected;
}

void ch582_send_keyboard_report(report_keyboard_t *report) {
    /* Boot keyboard report = [mods][reserved][key1..key6]; this matches the
     * stock A1 frame byte-for-byte. report->keys is the 6KRO slot array, valid
     * regardless of NKRO (it is the first member of the report union). */
    uint8_t buf[8];
    buf[0] = report->mods;
    buf[1] = 0; /* reserved */
    for (uint8_t i = 0; i < 6; i++) {
        buf[2 + i] = report->keys[i];
    }
    ch582_send_command(0xA1, buf, sizeof(buf));
    debug_kbd_tx_count++;
}

/* --- QMK Bluetooth driver API ------------------------------------------------
 * With BLUETOOTH_DRIVER = custom, QMK's host.c routes key reports to bt_driver
 * (these bluetooth_* functions) whenever the active connection host is
 * Bluetooth, and to the USB driver otherwise. The mode slider drives the active
 * host (see dip_switch_update_user). No host_set_driver hacks, no clobber, and
 * no manual USB/wireless routing -- core handles it. */
void bluetooth_init(void) {
    is_module_connected = false;
    sdStart(&CH582_SERIAL_DRIVER, &serial_cfg);
}

void bluetooth_task(void) {
    ch582_task();
}

bool bluetooth_is_connected(void) {
    return is_module_connected;
}

/* The module's wireless input is a 6KRO boot report (A1). Reporting NKRO as
 * unsupported makes host_can_send_nkro() return false while Bluetooth is the
 * active host, so QMK sends a plain boot report_keyboard_t via
 * bluetooth_send_keyboard() -- no NKRO->boot conversion needed here. USB keeps
 * NKRO (keyboard.json "nkro": true) when USB is the active host. */
bool bluetooth_can_send_nkro(void) {
    return false;
}

uint8_t bluetooth_keyboard_leds(void) {
    return host_leds;
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    ch582_send_keyboard_report(report);
}

void bluetooth_send_nkro(report_nkro_t *report) {
    /* Not reached: bluetooth_can_send_nkro() is false. Stubbed for safety. */
}

void bluetooth_send_mouse(report_mouse_t *report) {}

/* Consumer control (volume/media, e.g. the encoder) over the wireless link.
 * The encoder emits HID consumer usages; without forwarding them here they only
 * work over USB. The A1 keyboard opcode is analyzer-confirmed, but the module's
 * consumer opcode was NOT captured -- 0xA3 + 2-byte little-endian usage is a
 * best-effort guess mirroring the A1 framing. VERIFY against a stock-fw capture
 * of a volume key in BT mode and adjust the opcode/layout if it differs. */
void bluetooth_send_consumer(uint16_t usage) {
    if (!ch582_kbd_output_active()) return;
    uint8_t buf[2];
    buf[0] = usage & 0xFF;
    buf[1] = (usage >> 8) & 0xFF;
    ch582_send_command(0xA3, buf, sizeof(buf));
}

void bluetooth_send_system(uint16_t usage) {}

void ch582_send_command(uint8_t cmd, const uint8_t *params, uint8_t param_len) {
    static uint8_t tx_packet[TX_MAX_PAYLOAD + 2];
    if (param_len > TX_MAX_PAYLOAD) return;

    uint16_t sum  = cmd;
    tx_packet[0]  = cmd;
    for (uint8_t i = 0; i < param_len; i++) {
        tx_packet[1 + i] = params[i];
        sum += params[i];
    }
    uint8_t total_len             = param_len + 2;
    tx_packet[total_len - 1]      = (uint8_t)(sum & 0xFF);

    sdWrite(&CH582_SERIAL_DRIVER, tx_packet, total_len);
    debug_tx_packet_count++;
}

void ch582_set_profile(ch582_profile_t profile) {
    uint8_t param       = (uint8_t)profile;
    requested_profile   = param;
    connect_requested   = true;
    usb_mode            = false;
    is_module_connected = false;
    last_attempt_time   = timer_read();
    ch582_send_command(0xA6, &param, 1);
}

/* Pairing command, decoded from stock TX captures: a CONSTANT `0xA6 0x51`, sent
 * twice, which puts the CURRENTLY-SELECTED slot into pairing/advertising. It
 * carries no slot of its own (slot 2 and slot 3 pairing BOTH emitted `A6 51`), so
 * the target slot must already be selected via ch582_set_profile() first. This is
 * why the stock long-press only pairs the active slot. The old 0xA9/0xA1 guess was
 * wrong (0xA1 is the keystroke opcode). */
#define CH582_PAIR_PARAM 0x51

void ch582_enter_pairing(void) {
    uint8_t param       = CH582_PAIR_PARAM;
    is_module_connected = false;
    is_pairing          = true;
    /* Sent twice, matching stock (and the general 0xA6 retry behaviour). */
    ch582_send_command(0xA6, &param, 1);
    ch582_send_command(0xA6, &param, 1);
}

void ch582_pair(ch582_profile_t profile) {
    /* Select the slot, then put it into pairing. (On stock, select is the short
     * press and pairing the long press on the same slot; this convenience helper
     * does both for callers that just want "pair this slot now".) */
    ch582_set_profile(profile);
    ch582_enter_pairing();
}

void ch582_cancel_connect(void) {
    connect_requested = false;
    usb_mode          = true;
}

bool ch582_is_connected(void) {
    return is_module_connected;
}

uint8_t ch582_get_slot(void) {
    return connected_slot;
}

bool ch582_is_24g(void) {
    return !usb_mode && requested_profile == CH582_PROFILE_PEER_24G;
}

bool ch582_is_usb(void) {
    return usb_mode;
}

bool ch582_is_pairing(void) {
    return is_pairing;
}

uint8_t ch582_get_battery(void) {
    return battery_level;
}

uint8_t ch582_get_host_leds(void) {
    return host_leds;
}

/* Diagnostics for the LCD: whether keystrokes are being forwarded to the module
 * and how many have gone out since boot. */
bool ch582_output_active(void) {
    return ch582_kbd_output_active();
}

uint32_t ch582_get_kbd_tx_count(void) {
    return debug_kbd_tx_count;
}

/* Map a selected 0xA6 profile byte (0x31..0x34) to a 1..4 slot; 0 for 2.4G/none. */
static uint8_t profile_to_slot(uint8_t profile) {
    if (profile >= CH582_PROFILE_BT_1 && profile <= CH582_PROFILE_BT_4) {
        return profile - 0x30;
    }
    return 0;
}

void ch582_task(void) {
    /* Rolling 3-byte window over the RX stream. The module interleaves two frame
     * formats with no length prefix, and the stream can drop bytes on a burst, so
     * a fixed [type,data,cksum] state machine desyncs permanently. Matching on a
     * sliding window instead self-resynchronises and tolerates dropped bytes.
     *
     * Logic-analyzer-decoded against stock firmware. The ONLY trustworthy
     * connection signals are the 5B state transitions; everything else (61 0D 0A,
     * 5C battery, 5B 23) is periodic and streams regardless of link state:
     *   - 61 0D 0A   -> "a\r\n" periodic IDLE HEARTBEAT (NOT a disconnect: it is
     *                   emitted while connected too). Ignore for connection state.
     *   - 5A <led>   -> host keyboard LED bitmap (USB LED bits; bit1 = caps lock)
     *   - 5B <code>  -> connection state machine (code is a STATE, not a slot):
     *        32 = link established (connected);  31 = advertising/pairing;
     *        33/34 = connect ATTEMPT (link down, retrying);  23 = idle (ignore,
     *        it appears both connected and disconnected).
     *   - 5C <pct>   -> battery percent in decimal (0x64=100). PERIODIC and link-
     *                   independent (streams even while disconnected) -> never use
     *                   it as a "connected" signal. */
    static uint8_t b2 = 0, b1 = 0, b0 = 0;

    /* NOTE: the mode slider is wired directly to the CH582F, so BT/2.4G connection
     * is driven in hardware. QMK is passive here: it only parses the RX stream.
     * The old periodic 0xA6 retry created a feedback loop (each select bounced the
     * link: 61 0D 0A disconnect -> 5B 32 reconnect), making the state flip.
     *
     * BUT a *bounded* retry is still needed for cold boot: when the keyboard
     * powers up directly in BT mode the dip-switch select fires before the
     * CH582F finishes its own power-up and is dropped, leaving us stuck at
     * "idle" until the user manually re-selects (Fn+Q). Re-issue the select
     * ONLY while a connection is requested and not yet established -- gated on
     * !is_module_connected so it can never bounce a live link. We re-send the
     * raw 0xA6 (not ch582_set_profile) so the connection flags are untouched. */
    if (connect_requested && !is_module_connected &&
        timer_elapsed(last_attempt_time) >= CH582_CONNECT_RETRY_MS) {
        last_attempt_time = timer_read();
        uint8_t param = requested_profile;
        ch582_send_command(0xA6, &param, 1);
    }

    /* Periodically poll the module for battery level. The module only emits 5C
     * battery frames in response to an A6 53 request (the stock firmware polls the
     * same way). Poll regardless of connection mode: the gauge is valid in BT,
     * 2.4G and USB-charging states, and A6 53 is a status query that doesn't touch
     * the link. */
    if (timer_elapsed(last_battery_poll) >= CH582_BATTERY_POLL_MS) {
        last_battery_poll = timer_read();
        ch582_poll_status();
    }

    uint8_t c;
    uint8_t bytes_processed = 0;

    /* Drain the queue fully (capped to bound worst-case latency) so a connect
     * burst doesn't overflow the serial input buffer and shed bytes. */
    while (bytes_processed < 64) {
        if (chnReadTimeout(&CH582_SERIAL_DRIVER, &c, 1, TIME_IMMEDIATE) == 0) break;
        bytes_processed++;
        debug_rx_byte_count++;

        /* Freeze the first CH582_CAPTURE_N bytes of the live stream for the LCD
         * hex viewer (console output doesn't work on this board). */
        if (ch582_rx_capture_len < CH582_CAPTURE_N) {
            ch582_rx_capture[ch582_rx_capture_len++] = c;
        }

        b2 = b1;
        b1 = b0;
        b0 = c;

        bool matched = false;

        if (b2 == 0x61 && b1 == 0x0D && b0 == 0x0A) {
            /* Periodic idle heartbeat; emitted while connected AND disconnected,
             * so it carries no connection state. Don't touch is_module_connected. */
            debug_last_rx_type  = 0x61;
            debug_last_rx_data  = 0x0D;
            matched             = true;
        } else if ((b2 == 0x5A || b2 == 0x5B || b2 == 0x5C) &&
                   b0 == ((uint8_t)(b2 + b1))) {
            uint8_t type       = b2;
            uint8_t d          = b1;
            debug_last_rx_type = type;
            debug_last_rx_data = d;
            switch (type) {
                case 0x5A: /* host LED bitmap (caps lock = bit1) */
                    /* Only honor LED frames once a link exists. The boot power-up
                     * burst can coincidentally satisfy the weak 1-byte checksum
                     * for a bogus 5A frame, spuriously lighting Caps at boot. */
                    if (is_module_connected) host_leds = d;
                    break;
                case 0x5B: /* connection state code (NOT a slot number) */
                    switch (d) {
                        case 0x32: /* link established - the only "connected" signal */
                            is_module_connected = true;
                            is_pairing          = false;
                            connected_slot      = profile_to_slot(requested_profile);
                            break;
                        case 0x31: /* advertising / pairing, waiting for a device */
                            is_pairing          = true;
                            is_module_connected = false;
                            connected_slot      = 0;
                            host_leds           = 0; /* no link -> drop stale LED state */
                            break;
                        case 0x33: /* connect ATTEMPT - link is down and retrying */
                        case 0x34:
                            is_module_connected = false;
                            is_pairing          = false;
                            connected_slot      = 0;
                            host_leds           = 0; /* no link -> drop stale LED state */
                            break;
                        default:   /* 0x23 idle/finalize: periodic, appears both
                                    * connected and disconnected -> leave state */
                            break;
                    }
                    break;
                case 0x5C: /* battery percent; PERIODIC and link-independent (streams
                            * even while disconnected) -> do NOT touch connection state */
                    if (d <= 100) battery_level = d;
                    break;
            }
            matched = true;
        }

        if (matched) {
            debug_rx_packet_count++;
            /* Clear the window so the consumed bytes can't form a phantom overlap
             * match with the next byte. */
            b2 = b1 = b0 = 0;
        }
    }
}
