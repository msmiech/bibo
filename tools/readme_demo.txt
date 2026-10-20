# Input for the gameplay GIF of the README (Screenshots/bibo_gameplay.gif), recorded by tools/record_gameplay.sh.
# Shows the main menu, the graphics settings, a run on the track with a few F-key effects and the pause menu.
# Commands: wait <seconds>, press <key>, hold <key> <seconds>, quit (see BIBO/Util/DemoScript.hpp).
# Preview it in real time in the build folder: BIBO --demo ../tools/readme_demo.txt 960 540

# Main menu: Start, Settings, Quit
wait 1.5
press Down          # Settings
wait 0.4
press Enter
wait 0.8

# Settings menu: the graphics settings change the scene behind the menu right away.
# The arrow keys skip the Apply button while the display settings are unchanged.
press Down          # Resolution
wait 0.15
press Down          # Vertical sync
wait 0.15
press Down          # HDR
wait 0.15
press Down          # Bloom
wait 0.15
press Down          # Shadows
wait 0.3
press Enter         # shadows off
wait 0.8
press Enter         # shadows on
wait 0.3
press Down          # Normal mapping
wait 0.15
press Down          # Transparency
wait 0.15
press Down          # Bilinear texture filtering
wait 0.15
press Down          # Mipmapping
wait 0.15
press Down          # Wireframe
wait 0.3
press Enter         # wireframe on
wait 1
press Enter         # wireframe off
wait 0.4
press Escape        # back to the main menu (Settings is still selected)
wait 0.4
press Up            # Start
wait 0.3
press Enter
wait 0.3

# Run along the track: left with the track, then right past a hedgehog
hold Up 8
wait 1.5
press Space         # jump
wait 1.5
hold Left 1
wait 1.5
hold Right 0.4
wait 1
press Space
wait 1

# Wireframe (F3) for a moment
press F3
wait 1
press F3
wait 0.7

# Pause menu
press Escape
wait 1.2
press Enter         # Resume
wait 1
