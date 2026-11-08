#pragma once
#include <stdint.h>
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;
typedef int32_t SDL_Keycode;
typedef uint32_t SDL_Scancode;
typedef uint32_t SDL_JoystickID;
typedef uint32_t SDL_AudioDeviceID;
typedef struct SDL_Event { uint32_t type; } SDL_Event;
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_AUDIO 0x00000010u
