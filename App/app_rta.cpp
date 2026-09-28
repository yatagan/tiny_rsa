import rta;

extern "C" void app_rta_init(void) {
    rta::init();
}

extern "C" void app_rta_step(void) {
    rta::step();
}
