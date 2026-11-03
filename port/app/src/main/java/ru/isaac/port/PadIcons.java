package ru.isaac.port;

/*
 * WHICH PAD PICTURES A PAD GETS (the owner, 1.72): the GUIDE's slides show a
 * pad's buttons as the game's own pictures (gfx/ui/buttons.png), and the
 * pictures are baked in three families (tools/guide_slides.py FAMILIES):
 *
 *   PS      Sony (vendor 054c): the PS4 animation - cross, circle, Share, Options
 *   SWITCH  Nintendo (vendor 057e): Switch_Pro - B at the bottom, - and +; the
 *           Joy-Cons too (2006, 2007, alone or as the joined pair, JoyCons):
 *           their own animation (Switch_JoyCon) draws a single half held
 *           sideways as dots, which neither our pair nor our lone half is
 *   XBOX    everything else - Xbox pads, XInput clones such as an 8BitDo in its
 *           Xbox mode, the unknown - and no pad at all
 *
 * The vendor decides; a pad that hides it is judged by its name, then by the
 * name of its line in the SDL database (PadDb), and is an XBOX when nothing
 * says otherwise. A keyboard is no pad and has no family (NONE): it never
 * changes the pictures. Which pad's family is shown is the native side's
 * (pad.c shim_pad_icon_family: the slot of the last button press).
 *
 * The same family names the pad to the game: GCController.vendorName is what
 * iOS reports for such a pad (iosName), and the game picks its own button
 * prompts by that name - Xbox and DualShock names from its table, nothing for
 * a name it does not know (pad.c shim_pad_name).
 *
 * Pure: the desk (tools/paddesk/icons.sh) feeds it vendor ids and names.
 */
final class PadIcons {
    /* the order of guide_layout.h GD_FAMILY_NAMES (tools/guide_slides.py FAMILIES) */
    static final int NONE = -1, XBOX = 0, PS = 1, SWITCH = 2;
    static final String[] NAMES = { "xbox", "ps", "switch" };

    static final int SONY = 0x054c, NINTENDO = 0x057e, MICROSOFT = 0x045e;
    private static final int DUALSENSE = 0x0ce6, DUALSENSE_EDGE = 0x0df2;

    private PadIcons() { }

    /* a pad's family: by its vendor, else by its name, else by its database line's name; a keyboard has none */
    static int family(int vendor, int product, String name, String dbName, boolean keyboard) {
        if (keyboard) return NONE;
        vendor &= 0xffff;
        if (vendor == SONY) return PS;
        if (vendor == NINTENDO) return SWITCH;
        if (vendor == MICROSOFT) return XBOX;
        int byName = byName(name);
        if (byName != NONE) return byName;
        byName = byName(dbName);
        return byName != NONE ? byName : XBOX;
    }

    /* what a name says, NONE when it says nothing */
    static int byName(String name) {
        if (name == null || name.isEmpty()) return NONE;
        String n = name.toLowerCase(java.util.Locale.ROOT);
        if (n.contains("xbox") || n.contains("x-box") || n.contains("xinput")) return XBOX;
        if (n.contains("dualshock") || n.contains("dualsense") || n.contains("playstation")
                || n.contains("ps3") || n.contains("ps4") || n.contains("ps5")) return PS;
        if (n.contains("nintendo") || n.contains("switch") || n.contains("joy-con") || n.contains("joycon")
                || n.contains("pro controller")) return SWITCH;
        return NONE;
    }

    /* the name of a PadDb pick's database line (its report words: db "NAME" (...)), or "" */
    static String dbNameOf(String pickLine) {
        if (pickLine == null || !pickLine.startsWith("db \"")) return "";
        int end = pickLine.indexOf('"', 4);
        return end > 4 ? pickLine.substring(4, end) : "";
    }

    /*
     * The vendorName iOS gives such a pad. The game knows "Xbox Wireless
     * Controller" (its prompts: XboxOne), "DUALSHOCK 4 Wireless Controller" and
     * "DualSense Wireless Controller" (PS4); the Nintendo names it does not
     * know, and shows no prompts for them - as on iOS.
     */
    static String iosName(int family, int product, int joySide, boolean paired) {
        switch (family) {
            case PS:
                product &= 0xffff;
                return product == DUALSENSE || product == DUALSENSE_EDGE
                        ? "DualSense Wireless Controller" : "DUALSHOCK 4 Wireless Controller";
            case SWITCH:
                if (paired) return "Joy-Con (L/R)";
                if (joySide == JoyCons.LEFT) return "Joy-Con (L)";
                if (joySide == JoyCons.RIGHT) return "Joy-Con (R)";
                return "Pro Controller";
            default:
                return "Xbox Wireless Controller";
        }
    }
}
