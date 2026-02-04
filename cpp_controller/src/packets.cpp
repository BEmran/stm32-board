#include "packets.hpp"
#include <iostream>
#include <iomanip>

void print_state(const StatePkt &s)
{
  std::cout
      << "seq=" << std::setw(8) << s.seq << " "
      << std::showpos << std::fixed << std::setprecision(2)

      << "ax=" << std::setw(7) << s.ax << " "
      << "ay=" << std::setw(7) << s.ay << " "
      << "az=" << std::setw(7) << s.az << " "

      << "gx=" << std::setw(7) << s.gx << " "
      << "gy=" << std::setw(7) << s.gy << " "
      << "gz=" << std::setw(7) << s.gz << " "

      << "mx=" << std::setw(7) << s.mx << " "
      << "my=" << std::setw(7) << s.my << " "
      << "mz=" << std::setw(7) << s.mz << " "

      << "roll=" << std::setw(7) << s.roll << " "
      << "pitch=" << std::setw(7) << s.pitch << " "
      << "yaw=" << std::setw(7) << s.yaw << " "

      // back to integers (no sign, no fixed)
      << std::noshowpos << std::defaultfloat << std::setprecision(6)

      << "enc1=" << std::setw(4) << s.e1 << " "
      << "enc2=" << std::setw(4) << s.e2 << " "
      << "enc3=" << std::setw(4) << s.e3 << " "
      << "enc4=" << std::setw(4) << s.e4
      << "\n";
}