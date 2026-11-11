/* Compile-time verification of LG webOS SDL2 event struct ABI & scancodes */
#include <SDL2/SDL.h>
#include <SDL2/SDL_webOS.h>
#include <stddef.h>

_Static_assert(sizeof(SDL_Event) == 56, "SDL_Event must remain 56 bytes on 32-bit ARM");
_Static_assert(offsetof(SDL_KeyboardEvent, inputSource) == 12, "KeyboardEvent.inputSource @ 12");
_Static_assert(offsetof(SDL_KeyboardEvent, state) == 16, "KeyboardEvent.state @ 16");
_Static_assert(offsetof(SDL_KeyboardEvent, keysym) == 20, "KeyboardEvent.keysym @ 20");
_Static_assert(offsetof(SDL_TextEditingEvent, inputSource) == 12, "TextEditingEvent.inputSource @ 12");
_Static_assert(offsetof(SDL_TextEditingEvent, text) == 16, "TextEditingEvent.text @ 16");
_Static_assert(offsetof(SDL_TextInputEvent, inputSource) == 12, "TextInputEvent.inputSource @ 12");
_Static_assert(offsetof(SDL_TextInputEvent, text) == 16, "TextInputEvent.text @ 16");

_Static_assert(SDL_WEBOS_SCANCODE_CH_UP == 480, "CH_UP == 480");
_Static_assert(SDL_WEBOS_SCANCODE_CH_DOWN == 481, "CH_DOWN == 481");
_Static_assert(SDL_WEBOS_SCANCODE_BACK == 482, "BACK == 482");
_Static_assert(SDL_WEBOS_SCANCODE_CURSOR_SHOW == 484, "CURSOR_SHOW == 484");
_Static_assert(SDL_WEBOS_SCANCODE_CURSOR_HIDE == 485, "CURSOR_HIDE == 485");
_Static_assert(SDL_WEBOS_SCANCODE_RED == 486, "RED == 486");
_Static_assert(SDL_WEBOS_SCANCODE_GREEN == 487, "GREEN == 487");
_Static_assert(SDL_WEBOS_SCANCODE_YELLOW == 488, "YELLOW == 488");
_Static_assert(SDL_WEBOS_SCANCODE_BLUE == 489, "BLUE == 489");
_Static_assert(SDL_WEBOS_SCANCODE_EXIT == 505, "EXIT == 505");
