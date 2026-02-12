#pragma once
#include "core/basic.hpp"
#include "connection/packets.hpp"

#include <string>

namespace helpper
{
    class Print
    {
    public:
        explicit Print(double duration);
        [[nodiscard]] bool check();
    private:
        double last_{0.0};
        double duration_{1.0};
    };

    std::string to_string(const core::Vec3d &vec);

    std::string to_string(const core::Angles &ang);

    std::string to_string(const core::Encoders &enc);

    std::string to_string(const core::MotorCommands &m);

    std::string to_string(const core::States &states);

    std::string to_string(const core::Actions &actions);

    std::string to_string(const connection::StatesPkt &pkt);
    std::string to_string(const connection::CmdPkt &pkt);
}
