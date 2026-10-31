Monster Party - Windows port
===================================

1. Put your own copy of the game in the "roms" folder, named exactly:
     Monster Party (USA).nes                              (required) (No-Intro, CRC32 Headered:fe202c44 Headerless:02b9e7c2)
     Parody World - Monster Party (Japan) (Proto).nes     (optional) (No-Intro, CRC32 Headered:dd7b44d6 Headerless:21e28f50)
   No ROMs are included.

2. Double-click monsterparty.exe.
   A settings window comes first: display, controls, difficulty, music, palettes, shaders.
   In game: F10 reset, F11 fullscreen, Tab fast forward, Esc leave fullscreen / back to the settings.

Your settings are saved in settings.ini in %APPDATA%\NESdsm\monsterparty\ (paste that into
File Explorer's address bar to open the folder). Your own palettes (.pal) and SoundFonts (.sf2)
go in its palettes\ and soundfonts\ folders; "Open my palettes folder..." and
"Open my SoundFonts folder..." in the settings window open them.

Messages from the game are written to monsterparty.log in the same folder. Started from a
command prompt, it prints them there instead.

Included:
  soundfonts\  GeneralUser GS by S. Christian Collins (licence: GeneralUser-GS-LICENSE.txt)
  shaders\     the presets of the Shader list, from libretro's glsl-shaders;
               each file has its own licence (many are GPL)
  palettes\    color palettes (JASC .pal files)

Needs 64-bit Windows 10 or 11. Shaders and 3D mode need OpenGL graphics drivers.
If Windows SmartScreen warns about an unknown program, choose "More info" > "Run anyway".
