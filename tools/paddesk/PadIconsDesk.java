package ru.isaac.port;

/*
 * The desk of PadIcons: which family of pad pictures a pad gets, and the name
 * the game is given for it. Vendor ids, the Joy-Con ids, names and database
 * lines, the unknown, the keyboard; and the families' order against the
 * header the GUIDE is drawn with (guide_layout.h GD_FAMILY_NAMES).
 *   bash tools/paddesk/icons.sh
 */
final class PadIconsDesk {
    private static int checks, fails;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            fails++;
            System.out.println("  FAIL " + what);
        }
    }

    private static void fam(int vendor, int product, String name, String db, int want, String what) {
        int have = PadIcons.family(vendor, product, name, db, false);
        check(have == want, what + ": " + have + ", want " + want);
    }

    public static void main(String[] args) throws Exception {
        final int X = PadIcons.XBOX, P = PadIcons.PS, S = PadIcons.SWITCH;
        /* by vendor, whatever the name says */
        fam(0x054c, 0x09cc, "Wireless Controller", "", P, "DualShock 4 v2");
        fam(0x054c, 0x0ce6, "DualSense Wireless Controller", "", P, "DualSense");
        fam(0x054c, 0x0268, "PLAYSTATION(R)3 Controller", "", P, "DualShock 3");
        fam(0x057e, 0x2009, "Pro Controller", "", S, "Switch Pro");
        fam(0x057e, 0x2006, "Joy-Con (L)", "", S, "Joy-Con L");
        fam(0x057e, 0x2007, "Joy-Con (R)", "", S, "Joy-Con R");
        fam(0x057e, 0x2009, "8BitDo Pro 2", "", S, "an 8BitDo in Switch mode (057e:2009)");
        fam(0x045e, 0x0b13, "Xbox Wireless Controller", "", X, "Xbox Series");
        fam(0x045e, 0x02ea, "Xbox One S Controller", "", X, "Xbox One S");
        fam(0x045e, 0x028e, "8BitDo Ultimate Wireless", "", X, "the owner's 8BitDo, Xbox version (XInput 045e:028e)");
        fam(0x2dc8, 0x3106, "8BitDo Ultimate 2C Wireless", "", X, "an 8BitDo under its own vendor");
        fam(0x045e, 0x028e, "Nintendo Switch Pro Controller", "", X, "a Microsoft id wins over the name");
        /* no vendor: the name, then the database line */
        fam(0, 0, "DUALSHOCK 4 Wireless Controller", "", P, "DS4 name, no ids");
        fam(0, 0, "Joy-Con (L)", "", S, "Joy-Con name, no ids");
        fam(0, 0, "Nintendo Switch Pro Controller", "", S, "Pro name, no ids");
        fam(0, 0, "Generic X-Box pad", "PS4 Controller", X, "the device's own name before the line's");
        fam(0x0f0d, 0x00ee, "HORIPAD", "PS4 Controller", P, "a Hori PS4 pad by its database line");
        fam(0x0f0d, 0x00c1, "HORIPAD S", "Nintendo Switch HORIPAD", S, "a Hori Switch pad by its line");
        /* the unknown: Xbox pictures */
        fam(0x1234, 0x5678, "Gamepad", "", X, "unknown");
        fam(0, 0, null, null, X, "nothing at all");
        fam(0x0079, 0x0011, "USB Gamepad", "Dragonrise Generic USB Joystick", X, "a generic pad with a line");
        /* the keyboard has no family */
        check(PadIcons.family(0x046d, 0xc31c, "Logitech USB Keyboard", "", true) == PadIcons.NONE, "keyboard");
        check(PadIcons.family(0x054c, 0x09cc, "Wireless Controller", "", true) == PadIcons.NONE,
                "a keyboard is no pad whatever its vendor");
        /* the database line's name, out of the pick's report words */
        check(PadIcons.dbNameOf("db \"PS4 Controller\" (SDL, exact) guid 0500").equals("PS4 Controller"), "dbNameOf");
        check(PadIcons.dbNameOf("android standard, guid 05000000").isEmpty(), "dbNameOf standard");
        check(PadIcons.dbNameOf(null).isEmpty(), "dbNameOf null");
        /* the names the game gets: the two it knows, and Nintendo's it does not */
        check(PadIcons.iosName(X, 0, JoyCons.NONE, false).equals("Xbox Wireless Controller"), "xbox name");
        check(PadIcons.iosName(P, 0x09cc, JoyCons.NONE, false).equals("DUALSHOCK 4 Wireless Controller"), "ds4 name");
        check(PadIcons.iosName(P, 0x0ce6, JoyCons.NONE, false).equals("DualSense Wireless Controller"), "ds5 name");
        check(PadIcons.iosName(S, 0x2009, JoyCons.NONE, false).equals("Pro Controller"), "pro name");
        check(PadIcons.iosName(S, 0x2006, JoyCons.LEFT, false).equals("Joy-Con (L)"), "joy-con L name");
        check(PadIcons.iosName(S, 0x2007, JoyCons.RIGHT, true).equals("Joy-Con (L/R)"), "joy-con pair name");
        /* the order of the families is the header's */
        String header = new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(args[0])), "UTF-8");
        StringBuilder want = new StringBuilder("#define GD_FAMILY_NAMES { ");
        for (int i = 0; i < PadIcons.NAMES.length; i++) want.append(i > 0 ? ", " : "").append('"').append(PadIcons.NAMES[i]).append('"');
        want.append(" }");
        check(header.contains(want.toString()), "guide_layout.h has " + want);
        check(PadIcons.NAMES[PadIcons.XBOX].equals("xbox") && PadIcons.NAMES[PadIcons.PS].equals("ps")
                && PadIcons.NAMES[PadIcons.SWITCH].equals("switch"), "constants name their families");
        System.out.println("PadIcons: " + checks + " checks, " + fails + " failed");
        if (fails > 0) System.exit(1);
    }
}
