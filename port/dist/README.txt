Monster Party - Linux port
=================================

1. Put your own copy of the game in the "roms" folder, named exactly:
     Monster Party (USA).nes                              (required) (No-Intro, CRC32 Headered:fe202c44 Headerless:02b9e7c2)
     Parody World - Monster Party (Japan) (Proto).nes     (optional) (No-Intro, CRC32 Headered:dd7b44d6 Headerless:21e28f50)
   No ROMs are included.

2. Run ./monsterparty (from a terminal, or double-click it in your file manager).
   A settings window comes first: display, controls, difficulty, music, palettes, shaders.
   In game: F10 reset, F11 fullscreen, Esc leave fullscreen / quit.

Your settings are saved in ~/.config/monsterparty/settings.ini. Your own palettes (.pal) and
SoundFonts (.sf2) can go in ~/.config/monsterparty/palettes/ and ~/.config/monsterparty/soundfonts/.

Included:
  soundfonts/  GeneralUser GS by S. Christian Collins (licence: GeneralUser-GS-LICENSE.txt)
  shaders/     the presets of the Shader list, from libretro's glsl-shaders;
               each file has its own licence (many are GPL)
  palettes/    colour palettes (JASC .pal files)

Needs a 64-bit Linux with OpenGL (only for shaders) and a desktop running X11 or Wayland.
