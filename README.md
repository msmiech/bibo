# Bear-In Bear-Out
Bear-In Bear-Out (BIBO) is a simple 3D game in which the objective is to complete a lap as fast as possible and avoiding obstacles

## Features
* Freely movable camera
** Locked mode = following the player's character (default)
** Unlocked mode = 3d flying camera (hold left mouse button)
* Movable objects with physics (based on Nvidia PhysX)
* Texture Mapping (including normal maps)
* Lighting and Materials
* Keyboard controls
** Arrow keys for navigation
** Space bar for jumping
** F-Keys for settings and debug features (F1 for help)
* Supports complex models (like the bear, see copyright in files)
* Multiple light sources
* HDR
* Bloom
* Animations
* Transparency
* Shadow Maps
* Animated textures (e.g. water)

## Implementation Details
### Language and Compiler
This project was written using C++11 using Microsoft Visual Studio 2013
For hardware graphics (modern) OpenGL 4.5 has been used
It was developed for Windows 8.1 and Windows 10 and tested using Nvidia GeForce graphics hardware
### Libraries and Dependencies
* Assimp 3.2
* DevIL 1.7.8
* glew 1.13.0
* glfw 3.1.2
* glm 0.9.7.3
* zlib 1.2.8
* Nvidia PhysX 3.3.4 SDK (removed from source to comply with copyright)

## Sources
### Literature
* "OpenGL SuperBible"
* learnopengl.com
* opengl-tutorial.org
* ogldev.atspace.co.uk
* open.gl
* lighthouse3d.com/tutorials
* learncpp.com
### Assets
* BearBrown - http://tf3dm.com/3d-model/bear-98965.html
* SnowTerrain - http://tf3dm.com/3d-model/snowy-terrain-46334.html
* Rock - http://tf3dm.com/3d-model/rock-86533.html
* Salmon - http://tf3dm.com/3d-model/salmon-44387.html
