#include "game/game.hh"

int main(int argc, char **argv) {
    logger_init();
    logprintln("App", "start");
    
    {
        Game local_game = Game();
        game = &local_game;
        game->init();
        while (game->is_running) {
            game->update();
        }
        
        game->cleanup();
    }
    
    bool mleak = false;
    if (Mem::times_alloced) {
        mleak = true;
        logprintln("Mem", "Memory leak detected: free not called %llu times", Mem::times_alloced);
    }
    
    if (!mleak) {
        logprintln("Mem", "No memory leaks detected");
    }
    
    logprintln("App", "end of main");
    logger_cleanup();
    return 0;
}
