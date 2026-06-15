#ifndef CUE_UI_H
#define CUE_UI_H

/*
 * ui.h — top-level UI state machine.
 *
 * Driven by:
 *   - encoder_poll()            → scroll selection up/down
 *   - debounce_fell(BTN_SELECT) → activate selected item
 *   - debounce_fell(BTN_MENU)   → pop back to previous screen
 *   - debounce_fell(BTN_PLAY)   → toggle play/pause
 *   - debounce_fell(BTN_PREV)   → previous track
 *   - debounce_fell(BTN_NEXT)   → next track
 *
 * ui_render() must be called every frame (after input updates) to
 * redraw the framebuffer; the caller then invokes display_flush().
 */

void ui_init(void);

void ui_handle_scroll(int delta);    /* +N = down, -N = up        */
void ui_handle_select(void);
void ui_handle_back(void);
void ui_handle_play(void);
void ui_handle_prev(void);
void ui_handle_next(void);

void ui_render(void);

#endif /* CUE_UI_H */
