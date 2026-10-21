# Input for the gameplay GIF of the README (Screenshots/bibo_gameplay.gif), recorded by tools/record_gameplay.sh.
# Shows the main menu, the conversation with the beaver, the bear walking up to the starting line, the countdown and the start of the race.
# Keep it short: the GIF should stay below 10 MB (about 20 seconds at the default size and frame rate of the script).
# Commands: wait <seconds>, press <key>, hold <key> <seconds>, quit (see BIBO/Util/DemoScript.hpp).
# Preview it in real time in the build folder: BIBO --demo ../tools/readme_demo.txt 960 540

# Main menu: Start
wait 1
press Up            # Start (the focused button may follow the mouse)
wait 0.3
press Enter
wait 0.3

# Talk to the beaver next to the starting line: the text appears letter by letter (it chatters, it does not speak)
hold Up 0.9
hold Left 0.6
wait 1.6
press E             # talk
wait 1.6
press E             # the rest of the page at once
wait 0.6
press E             # next page
wait 0.4
press E
wait 0.4
press E
wait 0.4
press E
wait 0.4
press E             # the question
wait 2
press E             # Yes: the bear walks up to the starting line, 3, 2, 1, GO!
wait 8.5

# The race
hold Up 2.5
wait 2.5
