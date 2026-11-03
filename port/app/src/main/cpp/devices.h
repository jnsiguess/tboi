/*
 * The layer's book of the game's input devices (devices.c, batch 2, U4).
 *
 * What a device of the game IS is taken from its class (its vtable), never
 * from its number: the numbers are the game's to hand out, and the readers
 * that guessed by them ("0 is the screen", "> 0 is a pad") were wrong on
 * every count - the keyboard is 0, the touch screen 0x10dead, a pad 2.
 */
#ifndef ISAAC_DEVICES_H
#define ISAAC_DEVICES_H

enum {
    DEVICE_NONE = 0,        /* no such device (or -1, "no device") */
    DEVICE_PAD,             /* KAGE::Input::DeviceGamepad 0xa91a10, one per GCController of ours */
    DEVICE_SCREEN,          /* DeviceiOS 0xa92d88: the touch screen and its overlay */
    DEVICE_KEYBOARD,        /* DeviceKeyboard 0xa94958: the hardware keyboard through the shell */
    DEVICE_OTHER            /* a class the port does not know */
};

/* the kind of the game's device `id` (frame thread) */
int shim_device_kind(int id);

/* the game's object of device `id`, or 0 (frame thread) */
unsigned long shim_device_object(int id);

/* the pad.c slot behind a pad device, or -1 (frame thread) */
int shim_device_pad_slot(int id);

/* the device player k sits on (player+0x19ec), or -1 */
int shim_player_device(int k);

/* the game's keyboard device, or -1 */
int shim_device_keyboard(void);

/* the game's device made for pad.c's slot, or -1 */
int shim_device_of_pad_slot(int slot);

/* what device `id` is or was in this process: "screen", "keyboard", "pad:<descriptor>"; 0 if unknown */
int shim_device_identity(int id, char *out, int room);

/* today's number of the device that is `what`, or -1 when it is not here */
int shim_device_of_identity(const char *what);

/* a word for a kind, for the report */
const char *shim_device_kind_name(int kind);

/* the table and the ledger now, without the report's watchers */
void shim_devices_refresh(void);

/* once between frames, from coop.c: the table, and the report's watchers */
void shim_devices_between_frames(void);

#endif
