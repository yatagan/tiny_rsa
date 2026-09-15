import max7219_demo;

extern "C" void app_max7219_demo_init(void) {
    max7219_demo::init();
}

extern "C" void app_max7219_demo_step(void) {
    max7219_demo::step();
}
