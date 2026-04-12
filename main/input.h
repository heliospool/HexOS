#ifndef INPUT_H_
#define INPUT_H_

esp_err_t input_init(void (*button_short_clicked_cb)(void), void (*button_long_pressed_cb)(void));

/**
 * Register a second physical button with a short-press callback.
 * The ISR service must already be installed before calling this.
 * @param gpio_num  GPIO number of the button (active low, internal pull-up enabled)
 * @param short_clicked_cb  Called (from LVGL timer context) on each press
 */
esp_err_t input_add_button(int gpio_num, void (*short_clicked_cb)(void));

#endif /* INPUT_H_ */
