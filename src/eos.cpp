/*  This file is part of aither.
    Copyright (C) 2015-17  Michael Nucci (michael.nucci@gmail.com)

    Aither is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Aither is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <iostream>     // cout
#include <cstdlib>      // exit()
#include "eos.hpp"

using std::cout;
using std::endl;
using std::cerr;

// Member functions for idealGas class
// These functions calculate values using the ideal gas equation of state
// P = rho * R * T
double idealGas::PressFromEnergy(const unique_ptr<thermodynamic> &thermo,
                                 const double &rho, const double &energy,
                                 const double &vel) const {
  const auto specEnergy = energy - 0.5 * vel *vel;
  const auto temperature = thermo->TemperatureFromSpecEnergy(specEnergy);
  return this->PressureRT(rho, temperature);
}

double idealGas::PressureRT(const double &rho,
                            const double &temperature) const {
  return temperature * rho / gammaRef_;
}

double idealGas::SpecEnergy(const unique_ptr<thermodynamic> &thermo,
                            const double &t) const {
  return thermo->SpecEnergy(t);
}

double idealGas::Energy(const double &specEn, const double &vel) const {
  return specEn + 0.5 * vel * vel;
}

double idealGas::Enthalpy(const unique_ptr<thermodynamic> &thermo,
                          const double &t, const double &vel) const {
  return thermo->SpecEnthalpy(t) + 0.5 * vel * vel;
}

double idealGas::SoS(const double &pressure, const double &rho) const {
  return sqrt(gammaRef_ * pressure / rho);
}

double idealGas::Temperature(const double &pressure,
                             const double &rho) const {
  return pressure * gammaRef_ / rho;
}

double idealGas::PressureDim(const double &rho,
                             const double &temperature) const {
  return rho * gasConst_ * temperature;
}

