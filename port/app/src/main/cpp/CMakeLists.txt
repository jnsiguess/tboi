cmake_minimum_required(VERSION 3.22)
project(isaacport C ASM)
add_library(isaacport SHARED loader.c shim.c objc.c uikit.c gl.c trace.c cxxstr.c cxxstream.c audio.c darwin.c sticks.c menu.c pack.c pad.c probe.c reset.c rumble.c hud.c text.c bag.c pocket.c saves.c safefile.c screen.c eid.c eid_glitch.c eid_craft.c items.c marked.c touchedit.c lifediff.c
        credits.c tweaks.c spawner.c spawner_stats.c spawner_teleport.c mods.c bossbars.c modset.c straps.c statsplus.c rangefix.c gpu.c bloom.c stretch.c texel.c usermods.c content.c json.c modcheck_jni.c portpatch.c coop.c devices.c twintrinkets.c hudgeom.c hudlayout.c objc_asm.S darwin_jmp.S)
# in the private build the reconnaissance is off entirely: no hooks in the
# game's code, no reading points out of frame buffers, no log for every message
# Gradle builds the release as RelWithDebInfo, so we ask about Debug
target_compile_definitions(isaacport PRIVATE ISAAC_DIAG=$<IF:$<CONFIG:Debug>,1,0>)

find_library(log-lib log)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(android-lib android)
find_library(aaudio-lib aaudio)
target_link_libraries(isaacport ${log-lib} ${egl-lib} ${gles-lib} ${android-lib} ${aaudio-lib})
