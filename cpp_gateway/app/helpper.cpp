#include "helpper.hpp"
#include "core/basic.hpp"
#include "utils/timestamp.h"

#include <format>

namespace helpper
{
    Print::Print(double duration) : duration_{duration}
    {
        last_ = utils::monotonic_now() - duration_;
    }

    bool Print::check()
    {
        double now = utils::monotonic_now();
        if (now - last_ > duration_)
        {
            last_ = now;
            return true;
        }
        return false;
    }

    std::string to_string(const core::Vec3d &vec)
    {
        return std::format("[x:{:+.2f}, y:{:+.2f}, z:{:+.2f}]", vec.x, vec.y, vec.z);
    }

    std::string to_string(const core::Angles &ang)
    {
        return std::format("[r:{:+.2f}, p:{:+.2f}, y:{:+.2f}]", ang.roll, ang.pitch, ang.yaw);
    }

    std::string to_string(const core::Encoders &enc)
    {
        return std::format("[{:+5d}, {:+5d}, {:+5d}, {:+5d}]", enc.e1, enc.e2, enc.e3, enc.e4);
    }

    std::string to_string(const core::MotorCommands &m)
    {
        return std::format("[{:+4d}, {:+4d}, {:+4d}, {:+4d}]", m.m1, m.m2, m.m3, m.m4);
    }

    std::string to_string(const core::States &states)
    {
        return std::format("acc= {:s}, gyro= {:s}, mag= {:s}, angle= {:s}, enc= {:s}, batt= {:+.2f}", to_string(states.imu.acc), to_string(states.imu.gyro), to_string(states.imu.mag), to_string(states.ang), to_string(states.enc), states.battery_voltage);
    }

    std::string to_string(const core::Actions &actions)
    {
        return std::format("motors= {:s}, beep_ms= {}, flags= {:#010b}",
                           to_string(actions.motors),
                           static_cast<unsigned>(actions.beep_ms),
                           static_cast<unsigned>(actions.flags));
    }

    std::string to_string(const connection::StatesPkt &pkt)
    {
        return std::format("seq= {}, t_mono_s= {}, {:s}", pkt.seq, pkt.t_mono_s, to_string(pkt.state));
    }

    std::string to_string(const connection::CmdPkt &pkt)
    {
        return std::format("seq= {}, {:s}", pkt.seq, to_string(pkt.actions));
    }

}
