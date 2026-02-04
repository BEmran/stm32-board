#include "controller.hpp"
#include <iostream>
#include <string>

static void usage(const char *exe)
{
    std::cout
        << "Usage: " << exe
        << " [--ip 127.0.0.1] [--state_port N] [--cmd_port N] [--hz 200] [--print_s 1.0]\n";
}

static bool extract_controller_cfg(int argc, char **argv, app::ControllerConfig& cfg)
{
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto need = [&](const char *name) -> std::string
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return std::string(argv[++i]);
        };

        if (a == "--ip")
            cfg.ip = need("--ip");
        else if (a == "--state_port")
            cfg.state_port = (uint16_t)std::stoi(need("--state_port"));
        else if (a == "--cmd_port")
            cfg.cmd_port = (uint16_t)std::stoi(need("--cmd_port"));
        else if (a == "--hz")
            cfg.hz = std::stod(need("--hz"));
        else if (a == "--print_s")
            cfg.print_period_s = std::stod(need("--print_s"));
        else if (a == "--help")
        {
            usage(argv[0]);
            return false;
        }
        else
        {
            std::cerr << "Unknown arg: " << a << "\n";
            usage(argv[0]);
            return false;
        }
    }
    return true;
}
int main(int argc, char **argv)
{
    app::ControllerConfig cfg;
    if (!extract_controller_cfg(argc, argv, cfg)) {
        return EXIT_FAILURE;
    }
    app::Controller controller(cfg);
    if (!controller.init())
        return EXIT_FAILURE;
    return controller.run();
}
