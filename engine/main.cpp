#include "runtime/engine.h"
//系统入口点

int main()
{
    Elish::Engine engine;
    engine.initialize();
    engine.run();

    return 0;
}
