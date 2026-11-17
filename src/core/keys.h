/* The app's own key codes: what the screens see, whatever the remote or keyboard sent. */
#ifndef SB_KEYS_H
#define SB_KEYS_H
enum {
  K_NONE = 0,
  K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK,
  K_PLAY, K_PAUSE, K_PLAYPAUSE, K_STOP, K_REW, K_FF,
  K_RED, K_GREEN, K_YELLOW, K_BLUE,
  K_CH_UP, K_CH_DOWN, K_EXIT,
  K_CURSOR_SHOW, K_CURSOR_HIDE,  /* the Magic Remote pointer appeared / went away */
  K_CHAR,                         /* typed text (login fields): the codepoint comes separately */
  K_INFO                          /* the remote's Info key: Stats for nerds on and off, anywhere */
};
#endif
