#include "App.hpp"

int main(int argc, char* argv[]) {
    App::initBasePath((argc > 0 && argv != nullptr) ? argv[0] : nullptr);
    App app;
    app.run();
    return 0;
}
