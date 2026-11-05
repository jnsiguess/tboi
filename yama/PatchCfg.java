package ru.isaac.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

/*
 * Yama's patch loader (Ported by: Yamadead and Claude AI), recovered from the patched classes.dex
 * with jadx and fixed to compile. It writes the 12-byte records of assets/port_patches.bin
 * (u32 group | u32 file offset in isaac.arm64 | u32 ARM64 word) into the image bytes before
 * the loader maps them. Groups are bits 0..31 of the mask built from port-patches.ini / mods.txt.
 * One call site: MainActivity, right after read("isaac.arm64").
 */
public final class PatchCfg {
    static void all(File file, String str, String[] strArr) {
        for (String str2 : str.split("\n")) {
            entry(file, str2, strArr);
        }
    }

    public static void apply(byte[] bArr, File file, byte[] bArr2) {
        String readAll;
        if (bArr == null || file == null || bArr2 == null) {
            return;
        }
        try {
            File file2 = new File(file, "port-patches.ini");
            if (file2.exists()) {
                readAll = readAll(file2);
            } else {
                writeDefault(file2);
                readAll = "\n";
            }
            String mods = mods(file, readAll);
            int i = has(mods, "norush", "1") ? 0 | 2 : 0;
            if (has(mods, "alwaysvoid", "1")) {
                i |= 4;
            }
            if (has(mods, "timemachine", "1")) {
                i |= 8;
            }
            if (has(mods, "nocurses", "1")) {
                i |= 16;
                if (has(mods, "keeplabyrinth", "1")) {
                    i |= 4096;
                }
            }
            if (has(mods, "keeplabyrinth", "1")) {
                i |= 4112;
            }
            if (has(mods, "foxyscare", "4")) {
                i |= 288;
            } else if (has(mods, "foxyscare", "3")) {
                i |= 160;
            } else if (has(mods, "foxyscare", "2")) {
                i |= 96;
            } else if (has(mods, "foxyscare", "1")) {
                i |= 32;
            }
            if (has(mods, "glitchcrown", "1")) {
                i |= 512;
            } else if (has(mods, "glitchcrown", "3")) {
                i |= 2048;
            } else if (has(mods, "glitchcrown", "2")) {
                i |= 1024;
            }
            if (has(mods, "rerollfix", "1")) {
                i |= 8192;
            }
            if (has(mods, "birthrightsprites", "1")) {
                i |= 16384;
            }
            if (has(mods, "birthrightsprites", "2")) {
                i |= 49152;
            }
            if (has(mods, "dollysprites", "1")) {
                i |= 65536;
            }
            if (has(mods, "dollycostume", "1")) {
                i |= 131072;
            }
            if (has(mods, "nojam", "1")) {
                i |= 262144;
            }
            if (has(mods, "mothercontinue", "1")) {
                i |= 524288;
            }
            if (has(mods, "deadgod", "1")) {
                i |= 4194304;
            }
            if (has(mods, "fpscounter", "1")) {
                i |= 8388608;
            }
            if (has(mods, "treasurestart", "1")) {
                i |= 16777216;
            }
            if (has(mods, "emotes", "1")) {
                i |= 33554432;
            }
            if (has(mods, "regretpedestals", "1")) {
                i |= 67108864;
            }
            if (has(mods, "crownwait", "1")) {
                i |= 134217728;
            }
            if (has(mods, "fetustears", "3")) {
                i |= Integer.MIN_VALUE;
            } else if (has(mods, "fetustears", "2")) {
                i |= 1073741824;
            } else if (has(mods, "fetustears", "1")) {
                i |= 268435456;
            }
            if (has(mods, "isaacboss", "1")) {
                i |= 536870912;
            }
            if (has(mods, "lostpennies", "1")) {
                i |= 1048576;
            }
            if (has(mods, "charslots", "1")) {
                i |= 2097152;
            }
            for (int i2 = 0; i2 + 12 <= bArr2.length; i2 += 12) {
                int i3 = bArr2[i2] & 255;
                if (i3 <= 31 && ((1 << i3) & i) != 0) {
                    int le32 = le32(bArr2, i2 + 4);
                    int le322 = le32(bArr2, i2 + 8);
                    if (le32 >= 0 && le32 + 4 <= bArr.length) {
                        bArr[le32] = (byte) le322;
                        bArr[le32 + 1] = (byte) (le322 >> 8);
                        bArr[le32 + 2] = (byte) (le322 >> 16);
                        bArr[le32 + 3] = (byte) (le322 >> 24);
                    }
                }
            }
        } catch (Throwable th) {
        }
    }

    static void clean(File file, File file2, String[] strArr) {
        try {
            File[] listFiles = file.listFiles();
            if (listFiles != null) {
                for (File file3 : listFiles) {
                    if (file3.getName().startsWith("port-patch-")) {
                        File[] listFiles2 = file3.listFiles();
                        if (listFiles2 != null) {
                            for (File file4 : listFiles2) {
                                file4.delete();
                            }
                        }
                        file3.delete();
                    }
                }
            }
            String[] split = strArr[2].split("\n");
            StringBuilder sb = new StringBuilder();
            for (String str : split) {
                if (!str.contains("port-patch-") && str.length() != 0) {
                    sb.append("\n");
                    sb.append(str);
                }
            }
            sb.append("\n");
            strArr[2] = sb.toString();
        } catch (Throwable unused) {
        }
    }

    /* JADX WARN: Multi-variable type inference failed */
    static void entry(File file, String str, String[] strArr) {
        try {
            String[] split = str.split("\\|");
            String str2 = split[0];
            String str3 = split[1];
            String str4 = split[2];
            String str5 = str2;
            if (split.length > 3) {
                str5 = split[3];
            }
            String concat = "port-".concat(str5);
            int indexOf = str2.indexOf("=");
            String str6 = null;
            if (indexOf >= 0) {
                str6 = str2.substring(indexOf + 1);
                str2 = str2.substring(0, indexOf);
            }
            File file2 = new File(file, concat);
            File file3 = new File(file2, "disable.it");
            File file4 = new File(file2, "state2.txt");
            if (!file2.exists()) {
                file2.mkdirs();
            }
            meta(file2, concat, str3, str4);
            if (file3.exists()) {
                file3.delete();
            }
            String str7 = get(file4);
            String str8 = strArr[2];
            int i = str8.contains("\non ".concat(concat).concat("\n")) ? 1 : str8.contains("\noff ".concat(concat).concat("\n")) ? 0 : -1;
            String iniVal = iniVal(strArr[0], str2);
            int on = (str6 == null ? on(iniVal) : iniVal.trim().equals(str6)) ? 1 : 0;
            int equals = str7 == null ? i : ("1".equals(str7) ? 1 : 0);
            int i2 = on;
            if (i >= 0 && i != equals) {
                i2 = i;
                if (i != on) {
                    String str9 = str6 != null ? str6 : "1";
                    if (i == 0) {
                        str9 = "0";
                    }
                    strArr[0] = setVal(strArr[0], str2, str9);
                }
            }
            put(file4, i2 == 0 ? "0" : "1");
            strArr[1] = strArr[1].concat(str2 + " menu=" + i + " prev=" + equals + " ini=" + iniVal + " now=" + i2 + "\n");
            String str10 = strArr[2];
            String concat2 = "\n".concat(i2 == 0 ? "off " : "on ").concat(concat).concat("\n");
            strArr[2] = i < 0 ? str10.concat(concat2.substring(1)) : str10.replace("\non ".concat(concat).concat("\n"), concat2).replace("\noff ".concat(concat).concat("\n"), concat2);
        } catch (Throwable th) {
        }
    }

    static String get(File file) {
        try {
            if (file.exists()) {
                return raw(file).trim();
            }
            return null;
        } catch (Throwable th) {
            return null;
        }
    }

    static boolean has(String str, String str2, String str3) {
        return str.contains("\n".concat(str2).concat("=").concat(str3));
    }

    static String iniVal(String str, String str2) {
        String concat = "\n".concat(str2).concat("=");
        int indexOf = str.indexOf(concat);
        if (indexOf < 0) {
            return null;
        }
        int length = concat.length() + indexOf;
        int indexOf2 = str.indexOf(10, length);
        if (indexOf2 < 0) {
            indexOf2 = str.length();
        }
        return str.substring(length, indexOf2).trim();
    }

    static boolean isOff(String str, String str2) {
        return has(str, str2, "0") || has(str, str2, "false") || has(str, str2, "off") || has(str, str2, "no");
    }

    static int le32(byte[] bArr, int i) {
        return (bArr[i] & 255) | ((bArr[i + 1] & 255) << 8) | ((bArr[i + 2] & 255) << 16) | ((bArr[i + 3] & 255) << 24);
    }

    static void meta(File file, String str, String str2, String str3) {
        try {
            byte[] bytes = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<metadata>\n\t<name>" + str2 + "</name>\n\t<directory>" + str + "</directory>\n\t<description>" + str3 + "</description>\n\t<version>1.0</version>\n\t<visibility>Public</visibility>\n</metadata>\n").getBytes("UTF-8");
            FileOutputStream fileOutputStream = new FileOutputStream(new File(file, "metadata.xml"));
            fileOutputStream.write(bytes);
            fileOutputStream.close();
        } catch (Throwable th) {
        }
    }

    static String mods(File file, String str) {
        File file2 = new File(file, "port-patches.ini");
        try {
            String raw = raw(file2);
            File file3 = new File(file, "mods");
            file3.mkdirs();
            File file4 = new File(file3, "mods.txt");
            String[] strArr = {raw, "status v26: menu = enabled in the MODS menu (1/0), prev = what the menu said at the last start, ini = the ini value, now = the result\n", norm(get(file4))};
            clean(file3, file4, strArr);
            all(file3, "norush|[Port] No time limits|Boss Rush door (20 min) and Hush door (30 min) always appear.\nalwaysvoid|[Port] Always Void|Void portal chances after bosses are always 100%.\ntimemachine|[Port] Time machine|Slot machines, beggars and similar machines run 5x faster while you stand on them.\nnocurses|[Port] No curses|Floor curses never happen.\nkeeplabyrinth|[Port] Keep Labyrinth curse|Keeps only the Labyrinth curse (extra-large floors); all other curses stay removed.\nfoxyscare|[Port] Foxy jumpscare|Rare random Clicker jumpscare (about once an hour). Other frequencies are in port-patches.ini.\nglitchcrown=1|[Port] Glitched Crown slower (12 frames)|Item pedestals swap every 12 frames instead of 6.|glitchcrown-12\nglitchcrown=2|[Port] Glitched Crown slower (20 frames)|Item pedestals swap every 20 frames instead of 6.|glitchcrown-20\nglitchcrown=3|[Port] Glitched Crown slower (30 frames)|Item pedestals swap every 30 frames instead of 6.|glitchcrown-30\ncrownwait|[Port] Glitched Crown respects wait|A pedestal cycled by Glitched Crown keeps its remaining pick-up wait.\nrerollfix|[Port] Reroll fix|A Restock Machine rerolls once, not twice, when bombed or when it explodes from overuse.\nnojam|[Port] No Donation Machine jam|The Donation Machine and the Greed Donation Machine never jam.\nbirthrightsprites|[Port] Unique Birthright sprites|A different Birthright pedestal icon per character. Needs the Unique Birthright Sprites mod.\ndollysprites|[Port] Unique Mr. Dolly sprites|A different Mr. Dolly pedestal icon per character.\ndollycostume|[Port] Unique Mr. Dolly costume|The Mr. Dolly costume is drawn in a different style per character.\nmothercontinue|[Port] Runs continue past Mother|Killing Mother also spawns the Sheol trapdoor, the Cathedral light beam and a void portal.\ndeadgod|[Port] DeadGod helper|Pedestal items you have never picked up glow gold and pulse.\nfpscounter|[Port] FPS counter|Shows the frame rate at the top of the screen.\ntreasurestart|[Port] Treasure Room start|On a new run the first floor is regenerated until a Treasure room touches the start room.\nemotes|[Port] Emote binds|Keyboard keys F2-F8 play the character's emote animations.\nregretpedestals|[Port] Regret pedestals|The items you did not take from a blind choice show what they were just before they vanish.\nfetustears|[Port] Unique C-Section fetuses|Fetus tears look different for each character (tainted use the normal look).\nisaacboss|[Port] Unique Isaac boss|The Isaac boss looks like the character you play (tainted use the normal look).\nlostpennies|[Port] Lost's Lost Pennies|The Lost and Tainted Lost can pick up golden hearts; each one bursts into 3-7 pennies instead.\ncharslots|[Port] Extra character slots (Fiend Folio)|Makes room for the 20 Fiend Folio characters (ids 41-60) in the game's character table. Needs the Fiend Folio players.xml.", strArr);
            all(file3, "norush|[Port] No time limits|Boss Rush door (20 min) and Hush door (30 min) always appear.\nalwaysvoid|[Port] Always Void|Void portal chances after bosses are always 100%.\ntimemachine|[Port] Time machine|Slot machines, beggars and similar machines run 5x faster while you stand on them.\nnocurses|[Port] No curses|Floor curses never happen.\nkeeplabyrinth|[Port] Keep Labyrinth curse|Keeps only the Labyrinth curse (extra-large floors); all other curses stay removed.\nfoxyscare|[Port] Foxy jumpscare|Rare random Clicker jumpscare (about once an hour). Other frequencies are in port-patches.ini.\nglitchcrown=1|[Port] Glitched Crown slower (12 frames)|Item pedestals swap every 12 frames instead of 6.|glitchcrown-12\nglitchcrown=2|[Port] Glitched Crown slower (20 frames)|Item pedestals swap every 20 frames instead of 6.|glitchcrown-20\nglitchcrown=3|[Port] Glitched Crown slower (30 frames)|Item pedestals swap every 30 frames instead of 6.|glitchcrown-30\ncrownwait|[Port] Glitched Crown respects wait|A pedestal cycled by Glitched Crown keeps its remaining pick-up wait.\nrerollfix|[Port] Reroll fix|A Restock Machine rerolls once, not twice, when bombed or when it explodes from overuse.\nnojam|[Port] No Donation Machine jam|The Donation Machine and the Greed Donation Machine never jam.\nbirthrightsprites|[Port] Unique Birthright sprites|A different Birthright pedestal icon per character. Needs the Unique Birthright Sprites mod.\ndollysprites|[Port] Unique Mr. Dolly sprites|A different Mr. Dolly pedestal icon per character.\ndollycostume|[Port] Unique Mr. Dolly costume|The Mr. Dolly costume is drawn in a different style per character.\nmothercontinue|[Port] Runs continue past Mother|Killing Mother also spawns the Sheol trapdoor, the Cathedral light beam and a void portal.\ndeadgod|[Port] DeadGod helper|Pedestal items you have never picked up glow gold and pulse.\nfpscounter|[Port] FPS counter|Shows the frame rate at the top of the screen.\ntreasurestart|[Port] Treasure Room start|On a new run the first floor is regenerated until a Treasure room touches the start room.\nemotes|[Port] Emote binds|Keyboard keys F2-F8 play the character's emote animations.\nregretpedestals|[Port] Regret pedestals|The items you did not take from a blind choice show what they were just before they vanish.\nfetustears|[Port] Unique C-Section fetuses|Fetus tears look different for each character (tainted use the normal look).\nisaacboss|[Port] Unique Isaac boss|The Isaac boss looks like the character you play (tainted use the normal look).\nlostpennies|[Port] Lost's Lost Pennies|The Lost and Tainted Lost can pick up golden hearts; each one bursts into 3-7 pennies instead.\ncharslots|[Port] Extra character slots (Fiend Folio)|Makes room for the 20 Fiend Folio characters (ids 41-60) in the game's character table. Needs the Fiend Folio players.xml.", strArr);
            String norm = norm(strArr[2]);
            if (!norm.equals(norm(get(file4)))) {
                put(file4, norm.substring(1));
            }
            File file5 = new File(file, "port-patches-status.txt");
            StringBuilder sb = new StringBuilder();
            sb.append(strArr[1]);
            sb.append("\nfiles with port-patch/disable in the name, or changed in the last 6 hours (scan v22):\n");
            File parentFile = file.getParentFile();
            if (parentFile != null) {
                scan(parentFile, sb, 0);
            } else {
                scan(file, sb, 0);
            }
            put(file5, sb.toString());
            String str2 = strArr[0];
            if (!str2.equals(raw)) {
                put(file2, str2);
            }
            return readAll(file2);
        } catch (Throwable unused) {
            return str;
        }
    }

    static String norm(String str) {
        if (str == null || str.length() == 0) {
            str = "# the player's mods, the strongest first: on|off <folder>";
        }
        return "\n".concat(str.replace("\r", "")).concat("\n");
    }

    static boolean on(String str) {
        return (str == null || str.isEmpty() || str.equals("0") || str.equalsIgnoreCase("false") || str.equalsIgnoreCase("off") || str.equalsIgnoreCase("no")) ? false : true;
    }

    static void put(File file, String str) {
        try {
            byte[] bytes = str.getBytes("UTF-8");
            FileOutputStream fileOutputStream = new FileOutputStream(file);
            fileOutputStream.write(bytes);
            fileOutputStream.close();
        } catch (Throwable th) {
        }
    }

    static String raw(File file) throws Exception {
        FileInputStream fileInputStream = new FileInputStream(file);
        ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();
        byte[] bArr = new byte[4096];
        while (true) {
            int read = fileInputStream.read(bArr);
            if (read <= 0) {
                fileInputStream.close();
                return new String(byteArrayOutputStream.toByteArray(), "UTF-8");
            }
            byteArrayOutputStream.write(bArr, 0, read);
        }
    }

    static String readAll(File file) throws Exception {
        FileInputStream fileInputStream = new FileInputStream(file);
        ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();
        byte[] bArr = new byte[4096];
        while (true) {
            int read = fileInputStream.read(bArr);
            if (read <= 0) {
                fileInputStream.close();
                return "\n".concat(new String(byteArrayOutputStream.toByteArray()).replace("\r", "").replace(" ", "").replace("\t", "").toLowerCase());
            }
            byteArrayOutputStream.write(bArr, 0, read);
        }
    }

    static void scan(File file, StringBuilder sb, int i) {
        File[] listFiles;
        if (i > 6 || sb.length() > 30000 || (listFiles = file.listFiles()) == null) {
            return;
        }
        long currentTimeMillis = System.currentTimeMillis() - 345600000;
        for (File file2 : listFiles) {
            if (file2.isDirectory()) {
                scan(file2, sb, i + 1);
            } else {
                String path = file2.getPath();
                if (path.contains("port-patch") || path.contains("disable") || file2.lastModified() > currentTimeMillis) {
                    sb.append(file2.getPath());
                    sb.append("  ");
                    sb.append(file2.length());
                    sb.append("  ");
                    sb.append(file2.lastModified());
                    sb.append("\n");
                }
            }
        }
    }

    static String setVal(String str, String str2, String str3) {
        String concat = "\n".concat(str2).concat("=");
        int indexOf = str.indexOf(concat);
        if (indexOf < 0) {
            return str.concat("\n").concat(str2).concat("=").concat(str3).concat("\n");
        }
        int length = concat.length() + indexOf;
        int indexOf2 = str.indexOf(10, length);
        if (indexOf2 < 0) {
            indexOf2 = str.length();
        }
        return str.substring(0, length).concat(str3).concat(str.substring(indexOf2));
    }

    static void writeDefault(File file) {
        try {
            FileOutputStream fileOutputStream = new FileOutputStream(file);
            fileOutputStream.write("# Ported by: Yamadead and Claude AI\n#\n# Most patches also show up in the game's own MODS menu as \"[Port] ...\" entries (all start disabled). You can switch a patch either there or here; the last change wins. Restart the game after changing.\n#\n# Binary patches applied to the game when it starts.\n# 1 = on, 0 = off (everything is off by default). Change a value, then close the game completely and open it again.\n# Put each setting on its own line, with nothing after the value.\n\n# No time limits: Boss Rush door (20 min) and Hush door (30 min) always appear\nnorush=0\n\n# Void portal chances after bosses are always 100% (where the game rolls one)\nalwaysvoid=0\n\n# Time machine: slot machines, beggars and similar machines run 5x faster while you stand on them\ntimemachine=0\n\n# No curses: the floor curses (Darkness, Labyrinth, Lost, Unknown, Maze, Blind, Cursed rooms, Giant) never happen\nnocurses=0\n# Keep the Labyrinth curse (extra-large floors) while all other curses stay removed. Works on its own: it also removes the other curses, so nocurses does not need to be on. 0 = off, 1 = on\nkeeplabyrinth=0\n\n# Random Foxy jumpscare: now and then the Clicker effect (your edited giantbook art + sound) plays by itself\n# 0 = off, 1 = rare (about once an hour), 2 = normal (about every 9 minutes), 3 = test (about every 30 seconds), 4 = debug (about every 2 seconds)\nfoxyscare=0\n\n# Glitched Crown: item pedestals swap slower (vanilla swaps every 6 frames)\n# 0 = off (vanilla speed), 1 = a bit slower (12 frames), 2 = slower (20 frames), 3 = slowest (30 frames)\nglitchcrown=0\n\n# Reroll fix: a Restock Machine rerolls the items once, not twice, when it is bombed and when it explodes from being used too much (fixed in later game versions)\nrerollfix=0\n\n# No Donation Machine jam: the Donation Machine and the Greed Donation Machine never jam (an already jammed one works again). 0 = off, 1 = on\nnojam=0\n\n# Unique Birthright sprites: the Birthright pedestal shows a different icon for each character (41 vanilla characters).\n# Needs the Unique Birthright Sprites mod added in the MODS list (its resources supply the pictures). 0 = off, 1 = on, 2 = test (every character gets the normal Birthright picture)\nbirthrightsprites=0\n\n# Unique Mr. Dolly sprites: the Mr. Dolly pedestal shows a different icon for each character. 0 = off, 1 = on\ndollysprites=0\n\n# Unique Mr. Dolly costume: the Mr. Dolly your character wears is drawn in a different style for each character. 0 = off, 1 = on\ndollycostume=0\n\n# Runs Continue Past Mother: killing Mother also spawns the Sheol trapdoor, the Cathedral light beam and (50%) a void portal; taking them from Corpse II / Corpse XL leads to Sheol / Cathedral. No Hush door. Off during challenges. 0 = off, 1 = on\nmothercontinue=0\n\n# DeadGod Helper: item pedestals whose item you have never picked up (in the game's own collection list of this save slot) glow gold and pulse. Not shown during challenges or under Curse of the Blind. 0 = off, 1 = on\ndeadgod=0\n\n# FPS counter: shows \"FPS: 60\" at the top of the screen (white above 30, yellow 30 or below, red 15 or below). 0 = off, 1 = on\nfpscounter=0\n\n# Guaranteed Treasure Room start (port of Auto Reseed): on a new non-challenge run the first floor is regenerated until a Treasure room touches the start room (gives up after 300 tries). 0 = off, 1 = on\ntreasurestart=0\n\n# Emote binds (port of the Emote Binds mod): keyboard keys play the player's emote animations. F2 Light Travel, F3 Fall, F4 Glitch, F5 Jump, F6 Happy, F7 Sad, F8 Death. Needs a keyboard (or a keyboard overlay). Happy/Sad/Death also play their sounds. No Blood Explode. 0 = off, 1 = on\nemotes=0\n\n# Regret pedestals (reduced port of the Regret Pedestals mod): when you take one item of a blind choice (question mark pedestals, e.g. on Dross floors or under Curse of the Blind), the other items show what they were for about 2/3 of a second before they disappear. 0 = off, 1 = on\nregretpedestals=0\n\n# Glitched Crown respects wait time (port of the mod): when Glitched Crown cycles an item pedestal, the pedestal keeps its remaining pick-up wait instead of being reset, so a freshly spawned item can't be grabbed early by cycling. 0 = off, 1 = on\ncrownwait=0\n\n# Unique C-Section fetuses (port of the mod): the fetus tears of C-Section / Fetus weapon get a different look for each character (tainted characters use the normal character's look). Based on the first player. Needs the files added by this patch. 0 = off, 1 = on, 2 and 3 = crash-test modes (2 = without the new animation file, 3 = hook only)\nfetustears=0\n\n# Unique Isaac boss fight (port of the mod): the Isaac boss looks like the character you play (tainted use the normal look). Corpse / Abel / Incubus / Esau Jr extras of the original mod are not included. 0 = off, 1 = on\nisaacboss=0\n\n# Lost's Lost Pennies (port of the mod): The Lost and Tainted Lost can pick up golden hearts, and each one bursts into 3-7 pennies instead of being added. 0 = off, 1 = on\nlostpennies=0\n\n# Extra character slots (Fiend Folio): the game's character table gets 61 entries instead of 41 so the Fiend Folio players (ids 41-60) fit. 0 = off, 1 = on\ncharslots=0\n".getBytes());
            fileOutputStream.close();
        } catch (Throwable th) {
        }
    }
}
